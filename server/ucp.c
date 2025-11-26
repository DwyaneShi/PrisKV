#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <ucp/api/ucp.h>

#include "priskv-protocol.h"
#include "priskv-protocol-helper.h"
#include "priskv-log.h"
#include "priskv-utils.h"
#include "priskv-threads.h"
#include "priskv-event.h"
#include "memory.h"
#include "kv.h"
#include "ucp.h"

typedef struct priskv_ucp_mem {
    char name[32];
    uint8_t *buf;
    uint32_t buf_size;
    ucp_mem_h memh;
    void *rkey_buf;
    size_t rkey_len;
} priskv_ucp_mem;

typedef enum priskv_ucp_mem_type {
    PRISKV_UCP_MEM_REQ,
    PRISKV_UCP_MEM_RESP,
    PRISKV_UCP_MEM_KEYS,
    PRISKV_UCP_MEM_MAX
} priskv_ucp_mem_type;

typedef struct priskv_ucp_conn {
    ucp_ep_h ep;
    priskv_ucp_conn_cap conn_cap;
    void *kv;
    uint8_t *value_base;
    priskv_ucp_mem rmem[PRISKV_UCP_MEM_MAX];
} priskv_ucp_conn;

typedef struct priskv_ucp_server {
    int epollfd;
    void *kv;
    ucp_context_h context;
    ucp_worker_h worker;
    ucp_listener_h listener;
} priskv_ucp_server;

static priskv_ucp_server g_server = {
    .epollfd = -1,
};

uint32_t g_slow_query_threshold_latency_us = SLOW_QUERY_THRESHOLD_LATENCY_US;

static uint8_t priskv_ucp_am_id_req = 1;
static uint8_t priskv_ucp_am_id_resp = 2;

typedef struct ucp_rma_seg {
    ucp_rkey_h rkey;
    uint64_t addr;
    uint32_t length;
} ucp_rma_seg;

static void ucp_destroy_segs(ucp_rma_seg *segs, uint16_t n)
{
    if (!segs) return;
    for (uint16_t i = 0; i < n; i++) {
        if (segs[i].rkey) ucp_rkey_destroy(segs[i].rkey);
    }
    free(segs);
}

static ucp_rma_seg *ucp_unpack_segs(ucp_worker_h worker, const priskv_request *req, const uint8_t *keyptr)
{
    uint16_t nsgl = be16toh(req->nsgl);
    const priskv_keyed_sgl *sgls = req->sgls;
    const uint8_t *p = keyptr + be16toh(req->key_length);
    ucp_rma_seg *segs = calloc(nsgl, sizeof(*segs));
    if (!segs) return NULL;
    for (uint16_t i = 0; i < nsgl; i++) {
        uint16_t rkey_len_be;
        memcpy(&rkey_len_be, p, sizeof(rkey_len_be));
        uint16_t rkey_len = be16toh(rkey_len_be);
        p += sizeof(rkey_len_be);
        ucp_rkey_h rkey;
        if (ucp_ep_rkey_unpack(NULL, p, &rkey) != UCS_OK) {
            ucp_destroy_segs(segs, i);
            return NULL;
        }
        segs[i].rkey = rkey;
        segs[i].addr = be64toh(sgls[i].addr);
        segs[i].length = be32toh(sgls[i].length);
        p += rkey_len;
    }
    return segs;
}

static ucs_status_t priskv_ucp_am_req_cb(void *arg, const void *header, size_t header_length, void *data, size_t length, const ucp_am_recv_param_t *param)
{
    priskv_request *req = (priskv_request *)data;
    ucp_ep_h reply_ep = param->reply_ep;
    uint16_t nsgl = be16toh(req->nsgl);
    uint16_t keylen = be16toh(req->key_length);
    uint64_t request_id = be64toh(req->request_id);
    uint16_t command = be16toh(req->command);
    uint8_t *keyptr = priskv_request_key(req, nsgl);
    priskv_response resp = {0};
    resp.request_id = htobe64(request_id);
    resp.timeout = req->timeout;
    resp.status = htobe16(PRISKV_RESP_STATUS_OK);
    uint32_t total_len = priskv_sgl_size_from_be((priskv_keyed_sgl *)req->sgls, nsgl);
    ucp_rma_seg *segs = NULL;
    void *keynode = NULL;
    uint8_t *value_ptr = NULL;
    uint32_t valuelen = 0;
    ucp_request_param_t rparam;
    memset(&rparam, 0, sizeof(rparam));
    switch (command) {
    case PRISKV_COMMAND_GET: {
        int rc = priskv_get_key(g_server.kv, keyptr, keylen, &value_ptr, &valuelen, &keynode);
        if (rc != PRISKV_RESP_STATUS_OK) {
            resp.status = htobe16(rc);
            resp.length = htobe32(0);
            goto send_resp;
        }
        if (total_len < valuelen) {
            resp.status = htobe16(PRISKV_RESP_STATUS_VALUE_TOO_BIG);
            resp.length = htobe32(valuelen);
            priskv_get_key_end(keynode);
            goto send_resp;
        }
        segs = ucp_unpack_segs(g_server.worker, req, keyptr);
        if (!segs) {
            resp.status = htobe16(PRISKV_RESP_STATUS_SERVER_ERROR);
            resp.length = htobe32(0);
            priskv_get_key_end(keynode);
            goto send_resp;
        }
        uint64_t off = 0;
        for (uint16_t i = 0; i < nsgl; i++) {
            size_t len = segs[i].length;
            void *r = ucp_put_nbx(reply_ep, value_ptr + off, len, segs[i].addr, segs[i].rkey, &rparam);
            if (UCS_PTR_IS_PTR(r)) {
                while (ucp_request_check_status(r) == UCS_INPROGRESS) {
                    ucp_worker_progress(g_server.worker);
                }
                ucp_request_free(r);
            }
            off += len;
        }
        priskv_get_key_end(keynode);
        resp.length = htobe32(valuelen);
        break;
    }
    case PRISKV_COMMAND_SET: {
        segs = ucp_unpack_segs(g_server.worker, req, keyptr);
        if (!segs) {
            resp.status = htobe16(PRISKV_RESP_STATUS_SERVER_ERROR);
            resp.length = htobe32(0);
            goto send_resp;
        }
        uint8_t *dst = NULL;
        int rc = priskv_set_key(g_server.kv, keyptr, keylen, &dst, total_len, be64toh(req->timeout), &keynode);
        if (rc != PRISKV_RESP_STATUS_OK || !keynode) {
            resp.status = htobe16(rc);
            resp.length = htobe32(0);
            goto send_resp;
        }
        uint64_t off = 0;
        for (uint16_t i = 0; i < nsgl; i++) {
            size_t len = segs[i].length;
            void *r = ucp_get_nbx(reply_ep, dst + off, len, segs[i].addr, segs[i].rkey, &rparam);
            if (UCS_PTR_IS_PTR(r)) {
                while (ucp_request_check_status(r) == UCS_INPROGRESS) {
                    ucp_worker_progress(g_server.worker);
                }
                ucp_request_free(r);
            }
            off += len;
        }
        priskv_set_key_end(keynode);
        resp.length = htobe32(total_len);
        break;
    }
    case PRISKV_COMMAND_DELETE: {
        int rc = priskv_delete_key(g_server.kv, keyptr, keylen);
        resp.status = htobe16(rc);
        resp.length = htobe32(0);
        break;
    }
    case PRISKV_COMMAND_TEST: {
        int rc = priskv_get_key(g_server.kv, keyptr, keylen, &value_ptr, &valuelen, &keynode);
        if (rc != PRISKV_RESP_STATUS_OK) {
            resp.status = htobe16(rc);
            resp.length = htobe32(0);
        } else {
            priskv_get_key_end(keynode);
            resp.status = htobe16(PRISKV_RESP_STATUS_OK);
            resp.length = htobe32(valuelen);
        }
        break;
    }
    case PRISKV_COMMAND_EXPIRE: {
        int rc = priskv_expire_key(g_server.kv, keyptr, keylen, be64toh(req->timeout));
        resp.status = htobe16(rc);
        resp.length = htobe32(0);
        break;
    }
    case PRISKV_COMMAND_NRKEYS: {
        uint32_t nkey = 0;
        uint8_t *regex = keyptr;
        uint16_t regexlen = keylen;
        uint32_t bufsize = 4096;
        uint8_t *buf = malloc(bufsize);
        uint32_t reallen = 0;
        int rc;
        if (!buf) {
            resp.status = htobe16(PRISKV_RESP_STATUS_NO_MEM);
            resp.length = htobe32(0);
            break;
        }
        while (1) {
            rc = priskv_get_keys(g_server.kv, regex, regexlen, buf, bufsize, &reallen, &nkey);
            if (rc == PRISKV_RESP_STATUS_VALUE_TOO_BIG) {
                uint32_t newsize = reallen + reallen / 8;
                uint8_t *nbuf = realloc(buf, newsize);
                if (!nbuf) { rc = PRISKV_RESP_STATUS_NO_MEM; break; }
                buf = nbuf;
                bufsize = newsize;
                continue;
            }
            break;
        }
        free(buf);
        if (rc != PRISKV_RESP_STATUS_OK) {
            resp.status = htobe16(rc);
            resp.length = htobe32(0);
        } else {
            resp.status = htobe16(PRISKV_RESP_STATUS_OK);
            resp.length = htobe32(nkey);
        }
        break;
    }
    case PRISKV_COMMAND_FLUSH: {
        uint32_t nkey = 0;
        uint8_t *regex = keyptr;
        uint16_t regexlen = keylen;
        int rc = priskv_flush_keys(g_server.kv, regex, regexlen, &nkey);
        if (rc != PRISKV_RESP_STATUS_OK) {
            resp.status = htobe16(rc);
            resp.length = htobe32(0);
        } else {
            resp.status = htobe16(PRISKV_RESP_STATUS_OK);
            resp.length = htobe32(nkey);
        }
        break;
    }
    case PRISKV_COMMAND_KEYS: {
        segs = ucp_unpack_segs(g_server.worker, req, keyptr);
        if (!segs || nsgl != 1) {
            resp.status = htobe16(PRISKV_RESP_STATUS_SERVER_ERROR);
            resp.length = htobe32(0);
            break;
        }
        uint32_t reallen = 0, nkey = 0;
        uint8_t *regex = keyptr;
        uint16_t regexlen = keylen;
        uint8_t *keysbuf = malloc(segs[0].length);
        if (!keysbuf) {
            resp.status = htobe16(PRISKV_RESP_STATUS_NO_MEM);
            resp.length = htobe32(0);
            break;
        }
        int rc = priskv_get_keys(g_server.kv, regex, regexlen, keysbuf, segs[0].length, &reallen, &nkey);
        if (rc != PRISKV_RESP_STATUS_OK) {
            resp.status = htobe16(rc);
            resp.length = htobe32(reallen);
            free(keysbuf);
            break;
        }
        void *r = ucp_put_nbx(reply_ep, keysbuf, reallen, segs[0].addr, segs[0].rkey, &rparam);
        if (UCS_PTR_IS_PTR(r)) {
            while (ucp_request_check_status(r) == UCS_INPROGRESS) {
                ucp_worker_progress(g_server.worker);
            }
            ucp_request_free(r);
        }
        free(keysbuf);
        resp.status = htobe16(PRISKV_RESP_STATUS_OK);
        resp.length = htobe32(reallen);
        break;
    }
    default:
        resp.status = htobe16(PRISKV_RESP_STATUS_NO_SUCH_COMMAND);
        resp.length = htobe32(0);
        break;
    }
send_resp:
    if (segs) ucp_destroy_segs(segs, nsgl);
    ucp_request_param_t sp;
    memset(&sp, 0, sizeof(sp));
    void *sr = ucp_am_send_nbx(reply_ep, priskv_ucp_am_id_resp, &resp, sizeof(resp), &sp);
    if (UCS_PTR_IS_PTR(sr)) {
        while (ucp_request_check_status(sr) == UCS_INPROGRESS) {
            ucp_worker_progress(g_server.worker);
        }
        ucp_request_free(sr);
    }
    if (!(param->recv_attr & UCP_AM_RECV_ATTR_FLAG_RNDV)) {
        ucp_am_data_release(g_server.worker, data);
    }
    return UCS_OK;
}

static void priskv_ucp_listener_cb(ucp_listener_h listener, void *arg, const ucp_conn_request_h request)
{
    ucp_ep_params_t ep_params;
    memset(&ep_params, 0, sizeof(ep_params));
    ep_params.field_mask = UCP_EP_PARAM_FIELD_CONN_REQUEST;
    ep_params.conn_request = request;
    ucp_ep_h ep;
    ucs_status_t status = ucp_ep_create(g_server.worker, &ep_params, &ep);
    if (status != UCS_OK) {
        return;
    }
}

int priskv_ucp_listen(char **addr, int naddr, int port, void *kv, priskv_ucp_conn_cap *cap)
{
    ucp_config_t *config;
    ucs_status_t status = ucp_config_read(NULL, NULL, &config);
    if (status != UCS_OK) {
        return -1;
    }

    ucp_params_t params;
    memset(&params, 0, sizeof(params));
    params.field_mask = UCP_PARAM_FIELD_FEATURES | UCP_PARAM_FIELD_REQUEST_SIZE | UCP_PARAM_FIELD_ESTIMATED_NUM_PPN;
    params.features = UCP_FEATURE_TAG | UCP_FEATURE_RMA | UCP_FEATURE_WAKEUP | UCP_FEATURE_AM;
    params.request_size = 0;
    params.estimated_num_ppn = 1;

    status = ucp_init(&params, config, &g_server.context);
    ucp_config_release(config);
    if (status != UCS_OK) {
        return -1;
    }

    ucp_worker_params_t wparams;
    memset(&wparams, 0, sizeof(wparams));
    wparams.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    wparams.thread_mode = UCS_THREAD_MODE_SINGLE;
    status = ucp_worker_create(g_server.context, &wparams, &g_server.worker);
    if (status != UCS_OK) {
        return -1;
    }

    ucp_am_handler_param_t hp;
    memset(&hp, 0, sizeof(hp));
    hp.id = priskv_ucp_am_id_req;
    hp.flags = UCP_AM_FLAG_WHOLE_MSG;
    hp.cb = priskv_ucp_am_req_cb;
    ucp_worker_set_am_handler(g_server.worker, &hp);

    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(port);
    if (naddr > 0 && addr[0]) {
        inet_pton(AF_INET, addr[0], &listen_addr.sin_addr);
    } else {
        listen_addr.sin_addr.s_addr = INADDR_ANY;
    }

    ucp_listener_params_t lparams;
    memset(&lparams, 0, sizeof(lparams));
    lparams.field_mask = UCP_LISTENER_PARAM_FIELD_SOCK_ADDR | UCP_LISTENER_PARAM_FIELD_ACCEPT_HANDLER;
    lparams.sockaddr.addr = (const struct sockaddr *)&listen_addr;
    lparams.sockaddr.addrlen = sizeof(listen_addr);
    lparams.accept_handler.cb = priskv_ucp_listener_cb;
    lparams.accept_handler.arg = NULL;
    status = ucp_listener_create(g_server.worker, &lparams, &g_server.listener);
    if (status != UCS_OK) {
        return -1;
    }

    g_server.kv = kv;
    int efd = -1;
    ucp_worker_get_efd(g_server.worker, &efd);
    g_server.epollfd = efd;
    ucp_worker_arm(g_server.worker);
    return 0;
}

int priskv_ucp_get_fd(void)
{
    return g_server.epollfd;
}

static void priskv_ucp_progress_once(void)
{
    ucp_worker_progress(g_server.worker);
}

void priskv_ucp_process(void)
{
    priskv_ucp_progress_once();
    ucp_worker_arm(g_server.worker);
}

void *priskv_ucp_get_kv(void)
{
    return g_server.kv;
}

priskv_ucp_listener *priskv_ucp_get_listeners(int *nlisteners)
{
    *nlisteners = 1;
    priskv_ucp_listener *ls = calloc(1, sizeof(*ls));
    if (!ls) return NULL;
    strncpy(ls->address, "ucp://", PRISKV_ADDR_LEN - 1);
    ls->nclients = 0;
    ls->clients = NULL;
    return ls;
}

void priskv_ucp_free_listeners(priskv_ucp_listener *listeners, int nlisteners)
{
    if (!listeners) return;
    free(listeners);
}
