#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
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
#include "backend/backend.h"
#include "transport.h"
#include "uthash.h"

typedef struct priskv_transport_mem {
    char name[32];
    uint8_t *buf;
    uint32_t buf_size;
    ucp_mem_h memh;
    void *rkey_buf;
    size_t rkey_len;
} priskv_transport_mem;

typedef enum priskv_transport_mem_type {
    PRISKV_TRANSPORT_MEM_REQ,
    PRISKV_TRANSPORT_MEM_RESP,
    PRISKV_TRANSPORT_MEM_KEYS,
    PRISKV_TRANSPORT_MEM_MAX
} priskv_transport_mem_type;

typedef struct priskv_transport_conn {
    ucp_ep_h ep;
    priskv_transport_conn_cap conn_cap;
    void *kv;
    uint8_t *value_base;
    priskv_transport_mem rmem[PRISKV_TRANSPORT_MEM_MAX];
} priskv_transport_conn;

typedef struct priskv_transport_server {
    int epollfd;
    void *kv;
    ucp_context_h context;
    ucp_worker_h worker;
    ucp_listener_h listener;
    priskv_transport_conn_cap default_cap;
} priskv_transport_server;

typedef struct {
        uint64_t capacity_be;
        uint16_t max_sgl_be;
        uint16_t max_key_length_be;
        uint16_t max_inflight_command_be;
} priskv_server_capability;

typedef struct priskv_transport_rma_seg {
    ucp_rkey_h rkey;
    uint64_t addr;
    uint32_t length;
} priskv_transport_rma_seg;

typedef struct priskv_transport_rw_work {
    ucp_ep_h ep;
    priskv_request *req;
    priskv_transport_rma_seg *segs;
    uint16_t nsgl;
    uint16_t completed;
    uint8_t *buf;
    uint32_t len;
    void (*end_cb)(void *);
    void *end_arg;
} priskv_transport_rw_work;

static priskv_transport_server g_server = {
    .epollfd = -1,
};

priskv_threadpool *g_threadpool;

uint32_t g_slow_query_threshold_latency_us = SLOW_QUERY_THRESHOLD_LATENCY_US;

static uint8_t priskv_transport_am_id_req = 1;
static uint8_t priskv_transport_am_id_resp = 2;
static uint8_t priskv_transport_am_id_info_req = 3;
static uint8_t priskv_transport_am_id_info_resp = 4;

static priskv_transport_conn *priskv_transport_conn_get(ucp_ep_h ep);
static void priskv_transport_conn_add(ucp_ep_h ep);
static void priskv_transport_conn_remove(ucp_ep_h ep);
static void priskv_transport_ep_err_cb(void *arg, ucp_ep_h ep, ucs_status_t status);
static void priskv_transport_send_done_cb(void *request, ucs_status_t status, void *user_data);
static ucs_status_t priskv_transport_am_info_req_cb(void *arg, const void *header, size_t header_length, void *data, size_t length, const ucp_am_recv_param_t *param);
static void *priskv_transport_am_send(ucp_ep_h ep, uint8_t am_id, const void *payload,
                                      size_t length, void (*cb)(void *, ucs_status_t, void *),
                                      void *user_data);

static void priskv_transport_destroy_segs(priskv_transport_rma_seg *segs, uint16_t n)
{
    if (!segs) return;
    for (uint16_t i = 0; i < n; i++) {
        if (segs[i].rkey) ucp_rkey_destroy(segs[i].rkey);
    }
    free(segs);
}

static priskv_transport_rma_seg *priskv_transport_unpack_segs(ucp_ep_h ep, const priskv_request *req, const uint8_t *keyptr)
{
    uint16_t nsgl = be16toh(req->nsgl);
    const priskv_keyed_sgl *sgls = req->sgls;
    const uint8_t *p = keyptr + be16toh(req->key_length);
    priskv_transport_rma_seg *segs = calloc(nsgl, sizeof(*segs));
    if (!segs) return NULL;
    for (uint16_t i = 0; i < nsgl; i++) {
        uint16_t rkey_len_be;
        memcpy(&rkey_len_be, p, sizeof(rkey_len_be));
        uint16_t rkey_len = be16toh(rkey_len_be);
        p += sizeof(rkey_len_be);
        ucp_rkey_h rkey;
        if (ucp_ep_rkey_unpack(ep, p, &rkey) != UCS_OK) {
            priskv_transport_destroy_segs(segs, i);
            return NULL;
        }
        segs[i].rkey = rkey;
        segs[i].addr = be64toh(sgls[i].addr);
        segs[i].length = be32toh(sgls[i].length);
        p += rkey_len;
    }
    return segs;
}

static void priskv_transport_rw_complete_cb(void *request, ucs_status_t status, void *user_data)
{
    priskv_transport_rw_work *w = (priskv_transport_rw_work *)user_data;
    if (!w) return;
    w->completed++;
    if (w->completed < w->nsgl) return;
    if (w->end_cb) w->end_cb(w->end_arg);
    struct timeval tv2;
    gettimeofday(&tv2, NULL);
    w->req->runtime.server_data_recv_time = tv2;
    priskv_response *rd = malloc(sizeof(*rd));
    if (rd) {
        rd->request_id = w->req->request_id;
        rd->timeout = w->req->timeout;
        rd->status = htobe16(PRISKV_RESP_STATUS_OK);
        rd->length = htobe32(w->len);
        void *r = priskv_transport_am_send(w->ep, priskv_transport_am_id_resp, rd, sizeof(*rd), priskv_transport_send_done_cb, rd);
    }
    priskv_transport_destroy_segs(w->segs, w->nsgl);
    free(w);
}

static int priskv_transport_rw_submit(ucp_ep_h ep, priskv_request *req, priskv_transport_rma_seg *segs, uint16_t nsgl,
                                uint8_t *buf, uint32_t len, void (*end_cb)(void *), void *end_arg,
                                int is_set)
{
    priskv_transport_rw_work *work = calloc(1, sizeof(*work));
    if (!work) {
        return -ENOMEM;
    }
    work->ep = ep;
    work->req = req;
    work->segs = segs;
    work->nsgl = nsgl;
    work->completed = 0;
    work->buf = buf;
    work->len = len;
    work->end_cb = end_cb;
    work->end_arg = end_arg;
    uint64_t off = 0;
    for (uint16_t i = 0; i < nsgl; i++) {
        size_t slen = segs[i].length;
        ucp_request_param_t p;
        memset(&p, 0, sizeof(p));
        p.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA;
        p.cb.send = priskv_transport_rw_complete_cb;
        p.user_data = work;
        if (is_set) {
            (void)ucp_get_nbx(ep, buf + off, slen, segs[i].addr, segs[i].rkey, &p);
        } else {
            (void)ucp_put_nbx(ep, buf + off, slen, segs[i].addr, segs[i].rkey, &p);
        }
        off += slen;
    }
    struct timeval tv3;
    gettimeofday(&tv3, NULL);
    req->runtime.server_data_send_time = tv3;
    return 0;
}

int priskv_transport_send_response(ucp_ep_h ep, uint64_t request_id, priskv_resp_status status, uint32_t length)
{
    priskv_response *resp = malloc(sizeof(*resp));
    if (!resp) return -ENOMEM;
    resp->request_id = htobe64(request_id);
    resp->status = htobe16(status);
    resp->length = htobe32(length);
    void *sr = priskv_transport_am_send(ep, priskv_transport_am_id_resp, resp, sizeof(*resp), priskv_transport_send_done_cb, resp);
    return 0;
}

int priskv_transport_rw_req(ucp_ep_h ep, priskv_request *req, uint8_t *buf, uint32_t valuelen, int is_set, void (*cb)(void *), void *cbarg)
{
    uint16_t nsgl = be16toh(req->nsgl);
    uint16_t keylen = be16toh(req->key_length);
    uint8_t *keyptr = priskv_request_key(req, nsgl);
    priskv_transport_rma_seg *segs = priskv_transport_unpack_segs(ep, req, keyptr);
    if (!segs) return -EINVAL;
    int rc = priskv_transport_rw_submit(ep, req, segs, nsgl, buf, valuelen, cb, cbarg, is_set);
    if (rc != 0) {
        priskv_transport_destroy_segs(segs, nsgl);
        return rc;
    }
    return 0;
}


static ucs_status_t priskv_transport_am_req_cb(void *arg, const void *header, size_t header_length, void *data, size_t length, const ucp_am_recv_param_t *param)
{
    uint32_t recv_attr = param->recv_attr;
    int is_rndv = !!(recv_attr & UCP_AM_RECV_ATTR_FLAG_RNDV);
    uint8_t *msg = (uint8_t *)data;
    size_t msg_len = length;
    uint8_t *owned_buf = NULL;

    if (is_rndv) {
        owned_buf = (uint8_t *)malloc(length);
        if (!owned_buf) {
            priskv_log_error("am_req_cb: no mem to receive RNDV data, length %zu\n", length);
            ucp_am_data_release(g_server.worker, data);
            return UCS_OK;
        }
        ucp_request_param_t rp;
        memset(&rp, 0, sizeof(rp));
        rp.op_attr_mask = UCP_OP_ATTR_FIELD_MEMORY_TYPE;
        rp.memory_type = UCS_MEMORY_TYPE_HOST;
        void *r = ucp_am_recv_data_nbx(g_server.worker, data, owned_buf, length, &rp);
        if (UCS_PTR_IS_PTR(r)) {
            while (ucp_request_check_status(r) == UCS_INPROGRESS) {
                ucp_worker_progress(g_server.worker);
            }
            ucp_request_free(r);
        }
        msg = owned_buf;
    }

    ucp_ep_h reply_ep = param->reply_ep;
    if (!reply_ep) {
        priskv_log_error("am_req_cb: reply_ep is NULL, recv_attr 0x%x; cannot respond\n", recv_attr);
        if (is_rndv) {
            ucp_am_data_release(g_server.worker, data);
        }
        if (owned_buf) free(owned_buf);
        return UCS_OK;
    }
    priskv_request *req = (priskv_request *)msg;
    uint16_t nsgl = be16toh(req->nsgl);
    uint16_t keylen = be16toh(req->key_length);
    uint64_t request_id = be64toh(req->request_id);
    uint16_t command = be16toh(req->command);
    uint16_t keyoff = priskv_request_key_off(nsgl);
    uint8_t *keyptr = priskv_request_key(req, nsgl);
    priskv_log_debug("priskv_transport_am_req_cb: recv am req, id %lu, command %u, key %.*s, nsgl %u, keylen %u\n", request_id, command, keylen, keyptr, nsgl, keylen);

    priskv_response *resp_dyn = malloc(sizeof(priskv_response));
    priskv_response resp_local = {0};
    priskv_response *resp = resp_dyn ? resp_dyn : &resp_local;
    resp->request_id = htobe64(request_id);
    resp->timeout = req->timeout;
    resp->status = htobe16(PRISKV_RESP_STATUS_OK);
    uint32_t total_len = priskv_sgl_size_from_be((priskv_keyed_sgl *)req->sgls, nsgl);
    priskv_transport_rma_seg *segs = NULL;
    void *keynode = NULL;
    uint8_t *value_ptr = NULL;
    uint32_t valuelen = 0;
    uint32_t nkey = 0;
    int rc = PRISKV_RESP_STATUS_OK;
    ucp_request_param_t rparam;
    memset(&rparam, 0, sizeof(rparam));

    priskv_transport_conn *conn = priskv_transport_conn_get(reply_ep);
    priskv_transport_conn_cap cap = conn ? conn->conn_cap : g_server.default_cap;

    if (msg_len < keyoff) {
        resp->status = htobe16(PRISKV_RESP_STATUS_INVALID_COMMAND);
        resp->length = htobe32(0);
        goto send_resp;
    }
    if (keylen == 0) {
        resp->status = htobe16(PRISKV_RESP_STATUS_KEY_EMPTY);
        resp->length = htobe32(0);
        goto send_resp;
    }
    if (keylen > cap.max_key_length) {
        resp->status = htobe16(PRISKV_RESP_STATUS_KEY_TOO_BIG);
        resp->length = htobe32(0);
        goto send_resp;
    }
    if (nsgl > cap.max_sgl) {
        resp->status = htobe16(PRISKV_RESP_STATUS_INVALID_SGL);
        resp->length = htobe32(0);
        goto send_resp;
    }

    switch (command) {
    case PRISKV_COMMAND_GET: {
        if (priskv_backend_tiering_enabled()) {
            priskv_resp_status rs;
            priskv_tiering_req *treq = priskv_tiering_req_new(NULL, req, keyptr, keylen, be64toh(req->timeout), PRISKV_COMMAND_GET, total_len, &rs);
            if (!treq) {
                resp->status = htobe16(rs);
                resp->length = htobe32(0);
                goto send_resp;
            }
            treq->transport_ep = reply_ep;
            priskv_backend_req_resubmit(treq);
            return UCS_OK;
        } else {
            rc = priskv_get_key(g_server.kv, keyptr, keylen, &value_ptr, &valuelen, &keynode);
            if (rc != PRISKV_RESP_STATUS_OK) {
                resp->status = htobe16(rc);
                resp->length = htobe32(0);
                goto send_resp;
            }
            if (total_len < valuelen) {
                resp->status = htobe16(PRISKV_RESP_STATUS_VALUE_TOO_BIG);
                resp->length = htobe32(valuelen);
                priskv_get_key_end(keynode);
                goto send_resp;
            }
            segs = priskv_transport_unpack_segs(reply_ep, req, keyptr);
            if (!segs) {
                resp->status = htobe16(PRISKV_RESP_STATUS_SERVER_ERROR);
                resp->length = htobe32(0);
                priskv_get_key_end(keynode);
                goto send_resp;
            }
            struct timeval tv;
            gettimeofday(&tv, NULL);
            req->runtime.server_rw_kv_time = tv;
            if (priskv_transport_rw_submit(reply_ep, req, segs, nsgl, value_ptr, valuelen, priskv_get_key_end, keynode, 0) != 0) {
                resp->status = htobe16(PRISKV_RESP_STATUS_NO_MEM);
                resp->length = htobe32(0);
                priskv_transport_destroy_segs(segs, nsgl);
                priskv_get_key_end(keynode);
                goto send_resp;
            }
            return UCS_OK;
        }
    }
    case PRISKV_COMMAND_SET: {
        if (priskv_backend_tiering_enabled()) {
            priskv_resp_status rs;
            priskv_tiering_req *treq = priskv_tiering_req_new(NULL, req, keyptr, keylen, be64toh(req->timeout), PRISKV_COMMAND_SET, total_len, &rs);
            if (!treq) {
                resp->status = htobe16(rs);
                resp->length = htobe32(0);
                goto send_resp;
            }
            treq->transport_ep = reply_ep;
            priskv_backend_req_resubmit(treq);
            return UCS_OK;
        } else {
            segs = priskv_transport_unpack_segs(reply_ep, req, keyptr);
            if (!segs) {
                resp->status = htobe16(PRISKV_RESP_STATUS_SERVER_ERROR);
                resp->length = htobe32(0);
                goto send_resp;
            }
            uint8_t *dst = NULL;
            rc = priskv_set_key(g_server.kv, keyptr, keylen, &dst, total_len, be64toh(req->timeout), &keynode);
            if (rc != PRISKV_RESP_STATUS_OK || !keynode) {
                resp->status = htobe16(rc);
                resp->length = htobe32(0);
                goto send_resp;
            }
            struct timeval tv;
            gettimeofday(&tv, NULL);
            req->runtime.server_rw_kv_time = tv;
            if (priskv_transport_rw_submit(reply_ep, req, segs, nsgl, dst, total_len, priskv_set_key_end, keynode, 1) != 0) {
                resp->status = htobe16(PRISKV_RESP_STATUS_NO_MEM);
                resp->length = htobe32(0);
                priskv_transport_destroy_segs(segs, nsgl);
                priskv_set_key_end(keynode);
                goto send_resp;
            }
            return UCS_OK;
        }
    }
    case PRISKV_COMMAND_DELETE: {
        if (priskv_backend_tiering_enabled()) {
            priskv_resp_status rs;
            priskv_tiering_req *treq = priskv_tiering_req_new(NULL, req, keyptr, keylen, 0, PRISKV_COMMAND_DELETE, 0, &rs);
            if (!treq) {
                resp->status = htobe16(rs);
                resp->length = htobe32(0);
                goto send_resp;
            }
            treq->transport_ep = reply_ep;
            priskv_backend_req_resubmit(treq);
            return UCS_OK;
        } else {
            rc = priskv_delete_key(g_server.kv, keyptr, keylen);
            resp->status = htobe16(rc);
            resp->length = htobe32(0);
            break;
        }
    }
    case PRISKV_COMMAND_TEST: {
        if (priskv_backend_tiering_enabled()) {
            priskv_resp_status rs;
            priskv_tiering_req *treq = priskv_tiering_req_new(NULL, req, keyptr, keylen, 0, PRISKV_COMMAND_TEST, 0, &rs);
            if (!treq) {
                resp->status = htobe16(rs);
                resp->length = htobe32(0);
                goto send_resp;
            }
            treq->transport_ep = reply_ep;
            priskv_backend_req_resubmit(treq);
            return UCS_OK;
        } else {
            rc = priskv_get_key(g_server.kv, keyptr, keylen, &value_ptr, &valuelen, &keynode);
            if (rc != PRISKV_RESP_STATUS_OK) {
                resp->status = htobe16(rc);
                resp->length = htobe32(0);
            } else {
                priskv_get_key_end(keynode);
                resp->status = htobe16(PRISKV_RESP_STATUS_OK);
                resp->length = htobe32(valuelen);
            }
            break;
        }
    }
    case PRISKV_COMMAND_EXPIRE: {
        rc = priskv_expire_key(g_server.kv, keyptr, keylen, be64toh(req->timeout));
        resp->status = htobe16(rc);
        resp->length = htobe32(0);
        break;
    }
    case PRISKV_COMMAND_NRKEYS: {
        rc = priskv_get_keys(g_server.kv, keyptr, keylen, NULL, 0, &valuelen, &nkey);
        /* PRISKV_RESP_STATUS_VALUE_TOO_BIG is expected */
        if (rc == PRISKV_RESP_STATUS_VALUE_TOO_BIG) {
            resp->status = htobe16(PRISKV_RESP_STATUS_OK);
            resp->length = htobe32(nkey);
            break;
        }
        resp->status = htobe16(rc);
        resp->length = htobe32(0);
        break;
    }
    case PRISKV_COMMAND_FLUSH: {
        rc = priskv_flush_keys(g_server.kv, keyptr, keylen, &nkey);
        resp->status = htobe16(rc);
        resp->length = htobe32(nkey);
        break;
    }
    case PRISKV_COMMAND_KEYS: {
        segs = priskv_transport_unpack_segs(reply_ep, req, keyptr);
        if (!segs || nsgl != 1) {
            resp->status = htobe16(PRISKV_RESP_STATUS_SERVER_ERROR);
            resp->length = htobe32(0);
            break;
        }
        uint32_t reallen = 0;
        uint8_t *keysbuf = malloc(segs[0].length);
        if (!keysbuf) {
            resp->status = htobe16(PRISKV_RESP_STATUS_NO_MEM);
            resp->length = htobe32(0);
            break;
        }
        rc = priskv_get_keys(g_server.kv, keyptr, keylen, keysbuf, segs[0].length, &reallen, &nkey);
        if (rc != PRISKV_RESP_STATUS_OK) {
            resp->status = htobe16(rc);
            resp->length = htobe32(reallen);
            free(keysbuf);
            break;
        }
        ucp_mem_map_params_t mparams;
        memset(&mparams, 0, sizeof(mparams));
        mparams.field_mask = UCP_MEM_MAP_PARAM_FIELD_ADDRESS | UCP_MEM_MAP_PARAM_FIELD_LENGTH;
        mparams.address = keysbuf;
        mparams.length = reallen;
        ucp_mem_h memh;
        if (ucp_mem_map(g_server.context, &mparams, &memh) != UCS_OK) {
            resp->status = htobe16(PRISKV_RESP_STATUS_SERVER_ERROR);
            resp->length = htobe32(0);
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
        ucp_mem_unmap(g_server.context, memh);
        free(keysbuf);
        resp->status = htobe16(PRISKV_RESP_STATUS_OK);
        resp->length = htobe32(reallen);
        break;
    }
    default:
        priskv_log_error("am_req_cb: unknown command %d\n", req->command);
        resp->status = htobe16(PRISKV_RESP_STATUS_NO_SUCH_COMMAND);
        resp->length = htobe32(0);
        break;
    }
send_resp:
    if (segs) priskv_transport_destroy_segs(segs, nsgl);
    if (resp_dyn) {
        priskv_transport_am_send(reply_ep, priskv_transport_am_id_resp, resp_dyn, sizeof(*resp_dyn), priskv_transport_send_done_cb, resp_dyn);
    } else {
        ucp_request_param_t sp2;
        memset(&sp2, 0, sizeof(sp2));
        void *sr = ucp_am_send_nbx(reply_ep, priskv_transport_am_id_resp, NULL, 0, &resp_local, sizeof(resp_local), &sp2);
        if (UCS_PTR_IS_PTR(sr)) {
            while (ucp_request_check_status(sr) == UCS_INPROGRESS) {
                ucp_worker_progress(g_server.worker);
            }
            ucp_request_free(sr);
        }
    }
    if (is_rndv) {
        ucp_am_data_release(g_server.worker, data);
    }
    if (owned_buf) free(owned_buf);
    return UCS_OK;
}

static void priskv_transport_listener_conn_cb(const ucp_conn_request_h request, void *arg)
{
    ucp_ep_params_t ep_params;
    memset(&ep_params, 0, sizeof(ep_params));
    ep_params.field_mask = UCP_EP_PARAM_FIELD_CONN_REQUEST | UCP_EP_PARAM_FIELD_ERR_HANDLER;
    ep_params.conn_request = request;
    ep_params.err_handler.cb = priskv_transport_ep_err_cb;
    ucp_ep_h ep;
    ucs_status_t status = ucp_ep_create(g_server.worker, &ep_params, &ep);
    if (status != UCS_OK) {
        return;
    }
    priskv_transport_conn_add(ep);
}

int priskv_transport_listen(char **addr, int naddr, int port, void *kv, priskv_transport_conn_cap *cap)
{
    ucp_config_t *config;
    ucs_status_t status = ucp_config_read(NULL, NULL, &config);
    if (status != UCS_OK) {
        priskv_log_error("priskv_transport_listen: failed to read config, status %s\n", ucs_status_string(status));
        return -1;
    }

    ucp_config_print(config, stdout, "UCP Config", UCS_CONFIG_PRINT_CONFIG);

    ucp_params_t params;
    memset(&params, 0, sizeof(params));
    params.field_mask = UCP_PARAM_FIELD_FEATURES | UCP_PARAM_FIELD_REQUEST_SIZE | UCP_PARAM_FIELD_ESTIMATED_NUM_PPN;
    params.features = UCP_FEATURE_TAG | UCP_FEATURE_RMA | UCP_FEATURE_WAKEUP | UCP_FEATURE_AM;
    params.request_size = 0;
    params.estimated_num_ppn = 1;

    status = ucp_init(&params, config, &g_server.context);
    ucp_config_release(config);
    if (status != UCS_OK) {
        priskv_log_error("priskv_transport_listen: failed to initialize context, status %s\n", ucs_status_string(status));
        return -1;
    }

    ucp_worker_params_t wparams;
    memset(&wparams, 0, sizeof(wparams));
    wparams.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    wparams.thread_mode = UCS_THREAD_MODE_SINGLE;
    status = ucp_worker_create(g_server.context, &wparams, &g_server.worker);
    if (status != UCS_OK) {
        priskv_log_error("priskv_transport_listen: failed to create worker, status %s\n", ucs_status_string(status));
        return -1;
    }

    ucp_am_handler_param_t hparam;
    memset(&hparam, 0, sizeof(hparam));
    hparam.field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID | UCP_AM_HANDLER_PARAM_FIELD_FLAGS |
                        UCP_AM_HANDLER_PARAM_FIELD_CB | UCP_AM_HANDLER_PARAM_FIELD_ARG;
    hparam.id = priskv_transport_am_id_req;
    hparam.flags = UCP_AM_FLAG_WHOLE_MSG;
    hparam.cb = priskv_transport_am_req_cb;
    hparam.arg = NULL;
    status = ucp_worker_set_am_recv_handler(g_server.worker, &hparam);
    if (status != UCS_OK) {
        priskv_log_error("priskv_transport_listen: failed to set AM recv handler, status %s\n", ucs_status_string(status));
        return -1;
    }

    memset(&hparam, 0, sizeof(hparam));
    hparam.field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID | UCP_AM_HANDLER_PARAM_FIELD_FLAGS |
                        UCP_AM_HANDLER_PARAM_FIELD_CB | UCP_AM_HANDLER_PARAM_FIELD_ARG;
    hparam.id = priskv_transport_am_id_info_req;
    hparam.flags = UCP_AM_FLAG_WHOLE_MSG;
    hparam.cb = priskv_transport_am_info_req_cb;
    hparam.arg = NULL;
    status = ucp_worker_set_am_recv_handler(g_server.worker, &hparam);
    if (status != UCS_OK) {
        priskv_log_error("priskv_transport_listen: failed to set AM info handler, status %s\n", ucs_status_string(status));
        return -1;
    }

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
    lparams.field_mask = UCP_LISTENER_PARAM_FIELD_SOCK_ADDR | UCP_LISTENER_PARAM_FIELD_CONN_HANDLER;
    lparams.sockaddr.addr = (const struct sockaddr *)&listen_addr;
    lparams.sockaddr.addrlen = sizeof(listen_addr);
    lparams.conn_handler.cb = priskv_transport_listener_conn_cb;
    lparams.conn_handler.arg = NULL;
    status = ucp_listener_create(g_server.worker, &lparams, &g_server.listener);
    if (status != UCS_OK) {
        priskv_log_error("priskv_transport_listen: failed to create listener, status %s\n", ucs_status_string(status));
        return -1;
    }

    g_server.kv = kv;
    if (cap) {
        g_server.default_cap = *cap;
    } else {
        g_server.default_cap.max_sgl = PRISKV_TRANSPORT_DEFAULT_SGL;
        g_server.default_cap.max_key_length = PRISKV_TRANSPORT_DEFAULT_KEY_LENGTH;
        g_server.default_cap.max_inflight_command = PRISKV_TRANSPORT_DEFAULT_INFLIGHT_COMMAND;
    }
    int efd = -1;
    ucp_worker_get_efd(g_server.worker, &efd);
    g_server.epollfd = efd;
    {
        ucs_status_t st;
        do {
            ucp_worker_progress(g_server.worker);
            st = ucp_worker_arm(g_server.worker);
        } while (st == UCS_ERR_BUSY);
    }
    {
        char ip[64] = {0};
        inet_ntop(AF_INET, &listen_addr.sin_addr, ip, sizeof(ip));
        int lport = ntohs(listen_addr.sin_port);
        const char *ver = ucp_get_version_string();
        priskv_log_info("UCP: listen on %s:%d, version %s\n", ip, lport, ver);
        priskv_log_info("UCP: features AM|RMA|TAG|WAKEUP, AM handlers req=%u info_req=%u resp=%u\n", priskv_transport_am_id_req, priskv_transport_am_id_info_req, priskv_transport_am_id_resp);
        ucp_context_print_info(g_server.context, stdout);
    }
    return 0;
}

int priskv_transport_get_fd(void)
{
    return g_server.epollfd;
}

static void priskv_transport_progress_once(void)
{
    ucp_worker_progress(g_server.worker);
}

void priskv_transport_process(void)
{
    priskv_transport_progress_once();
    {
        ucs_status_t st;
        do {
            ucp_worker_progress(g_server.worker);
            st = ucp_worker_arm(g_server.worker);
        } while (st == UCS_ERR_BUSY);
    }
}

void *priskv_transport_get_kv(void)
{
    return g_server.kv;
}

priskv_transport_listener *priskv_transport_get_listeners(int *nlisteners)
{
    *nlisteners = 1;
    priskv_transport_listener *ls = calloc(1, sizeof(*ls));
    if (!ls) return NULL;
    strncpy(ls->address, "ucp://", PRISKV_ADDR_LEN - 1);
    ls->nclients = 0;
    ls->clients = NULL;
    return ls;
}

void priskv_transport_free_listeners(priskv_transport_listener *listeners, int nlisteners)
{
    if (!listeners) return;
    free(listeners);
}
/* Use latest UCP AM APIs */
typedef struct ucp_conn_entry {
    ucp_ep_h ep;
    priskv_transport_conn *conn;
    UT_hash_handle hh;
} ucp_conn_entry;

static ucp_conn_entry *g_conn_map = NULL;

static priskv_transport_conn *priskv_transport_conn_get(ucp_ep_h ep)
{
    ucp_conn_entry *e = NULL;
    HASH_FIND_PTR(g_conn_map, &ep, e);
    return e ? e->conn : NULL;
}

static void priskv_transport_conn_add(ucp_ep_h ep)
{
    priskv_transport_conn *conn = calloc(1, sizeof(*conn));
    if (!conn) return;
    conn->ep = ep;
    conn->kv = g_server.kv;
    conn->conn_cap = g_server.default_cap;
    ucp_conn_entry *e = calloc(1, sizeof(*e));
    if (!e) { free(conn); return; }
    e->ep = ep;
    e->conn = conn;
    HASH_ADD_PTR(g_conn_map, ep, e);
}
static void priskv_transport_conn_remove(ucp_ep_h ep)
{
    ucp_conn_entry *e = NULL;
    HASH_FIND_PTR(g_conn_map, &ep, e);
    if (!e) return;
    priskv_transport_conn *conn = e->conn;
    if (conn) {
        for (int i = 0; i < PRISKV_TRANSPORT_MEM_MAX; i++) {
            priskv_transport_mem *m = &conn->rmem[i];
            if (m->memh) {
                ucp_mem_unmap(g_server.context, m->memh);
                m->memh = NULL;
            }
            if (m->buf) {
                free(m->buf);
                m->buf = NULL;
            }
            if (m->rkey_buf) {
                free(m->rkey_buf);
                m->rkey_buf = NULL;
            }
            m->buf_size = 0;
            m->rkey_len = 0;
        }
        free(conn);
    }
    HASH_DEL(g_conn_map, e);
    free(e);
}

static ucs_status_t priskv_transport_am_info_req_cb(void *arg, const void *header, size_t header_length, void *data, size_t length, const ucp_am_recv_param_t *param)
{
    ucp_ep_h ep = param->reply_ep;
    uint32_t recv_attr = param->recv_attr;
    int is_rndv = !!(recv_attr & UCP_AM_RECV_ATTR_FLAG_RNDV);
    if (!ep) {
        if (is_rndv) {
            ucp_am_data_release(g_server.worker, data);
        }
        return UCS_OK;
    }
    uint64_t capacity = priskv_get_value_blocks(g_server.kv) * priskv_get_value_block_size(g_server.kv);
    priskv_server_capability *cap = malloc(sizeof(*cap));
    if (!cap) {
        if (is_rndv) {
            ucp_am_data_release(g_server.worker, data);
        }
        return UCS_ERR_NO_MEMORY;
    }
    cap->capacity_be = htobe64(capacity);
    cap->max_sgl_be = htobe16(g_server.default_cap.max_sgl);
    cap->max_key_length_be = htobe16(g_server.default_cap.max_key_length);
    cap->max_inflight_command_be = htobe16(g_server.default_cap.max_inflight_command);
    priskv_log_info("priskv_transport_am_info_req_cb: capacity %" PRIu64 ", max_sgl %" PRIu16 ", max_key_length %" PRIu16 ", max_inflight_command %" PRIu16 "\n",
                    capacity, g_server.default_cap.max_sgl, g_server.default_cap.max_key_length, g_server.default_cap.max_inflight_command);
    ucp_request_param_t sp;
    memset(&sp, 0, sizeof(sp));
    sp.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA;
    sp.cb.send = priskv_transport_send_done_cb;
    sp.user_data = cap;
    void *r = ucp_am_send_nbx(ep, priskv_transport_am_id_info_resp, NULL, 0, cap, sizeof(*cap), &sp);
    if (UCS_PTR_IS_ERR(r)) {
        priskv_transport_send_done_cb(NULL, UCS_PTR_STATUS(r), (void *)cap);
    } else if (!UCS_PTR_IS_PTR(r)) {
        priskv_transport_send_done_cb(NULL, UCS_OK, (void *)cap);
    }
    if (is_rndv) {
        ucp_am_data_release(g_server.worker, data);
    }
    return UCS_OK;
}

static void priskv_transport_send_done_cb(void *request, ucs_status_t status, void *user_data)
{
    if (status != UCS_OK) {
        priskv_log_error("priskv_transport_send_done_cb: send failed, status %s\n", ucs_status_string(status));
    } else {
        priskv_log_debug("priskv_transport_send_done_cb: ok\n");
    }

    if (user_data) {
        free(user_data);
    }

    if (request) {
        ucp_request_free(request);
    }
}

static void priskv_transport_ep_err_cb(void *a, ucp_ep_h ep, ucs_status_t status)
{
    ucp_request_param_t p;
    memset(&p, 0, sizeof(p));
    p.op_attr_mask = UCP_OP_ATTR_FIELD_FLAGS;
    p.flags = UCP_EP_CLOSE_FLAG_FORCE;
    void *req = ucp_ep_close_nbx(ep, &p);
    if (UCS_PTR_IS_PTR(req)) {
        while (ucp_request_check_status(req) == UCS_INPROGRESS) {
            ucp_worker_progress(g_server.worker);
        }
        ucp_request_free(req);
    }
    priskv_transport_conn_remove(ep);
}
static void *priskv_transport_am_send(ucp_ep_h ep, uint8_t am_id, const void *payload, size_t length,
                                      void (*cb)(void *, ucs_status_t, void *), void *user_data)
{
    ucp_request_param_t sp;
    memset(&sp, 0, sizeof(sp));
    sp.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA;
    sp.cb.send = cb;
    sp.user_data = user_data;
    void *r = ucp_am_send_nbx(ep, am_id, NULL, 0, payload, length, &sp);
    if (UCS_PTR_IS_ERR(r)) {
        cb(NULL, r, user_data);
    } else if (!UCS_PTR_IS_PTR(r)) {
        cb(NULL, UCS_OK, user_data);
    }
    return r;
}
