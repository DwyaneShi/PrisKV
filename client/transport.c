#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ucp/api/ucp.h>

#include "priskv.h"
#include "priskv-protocol.h"
#include "priskv-protocol-helper.h"
#include "priskv-log.h"

typedef struct priskv_transport_client_impl {
    ucp_context_h context;
    ucp_worker_h worker;
    ucp_ep_h ep;
    int efd;
    int nqueue;
    size_t npending;
    void *keys_buf;
    size_t keys_buf_len;
    priskv_memory *keys_mem;
    struct priskv_client *owner;
    uint64_t capacity;
    uint16_t max_sgl;
    uint16_t max_key_length;
    uint16_t max_inflight_command;
} priskv_transport_client_impl;

struct priskv_client {
    priskv_transport_client_impl *impl;
};

typedef struct priskv_transport_memory_impl {
    ucp_mem_h memh;
    void *rkey_buf;
    size_t rkey_len;
    uint64_t iova;
    size_t length;
    ucp_context_h context;
} priskv_transport_memory_impl;

struct priskv_memory {
    priskv_transport_memory_impl *impl;
};

typedef struct pending_req {
    uint64_t id;
    priskv_generic_cb cb;
    priskv_req_command cmd;
    priskv_sgl *sgl;
    uint16_t nsgl;
    char *str;
    uint64_t timeout;
    priskv_memory **auto_mems;
} pending_req;

static uint8_t priskv_transport_am_id_req = 1;
static uint8_t priskv_transport_am_id_resp = 2;
static uint8_t priskv_transport_am_id_info_req = 3;
static uint8_t priskv_transport_am_id_info_resp = 4;

static int priskv_transport_send_am_req(priskv_client *client, const void *buf, size_t len);
static void *priskv_transport_build_req_buf(priskv_req_command cmd, const char *key, priskv_sgl *sgl, uint16_t nsgl,
                           uint64_t timeout, pending_req *preq, size_t *out_len);
static ucs_status_t priskv_transport_am_resp_cb(void *arg, const void *header, size_t header_length, void *data, size_t length, const ucp_am_recv_param_t *param);
static ucs_status_t priskv_transport_am_info_cb(void *arg, const void *header, size_t header_length, void *data, size_t length, const ucp_am_recv_param_t *param);
static void priskv_client_ep_err_cb(void *arg, ucp_ep_h ep, ucs_status_t status);

static pending_req *pending_req_create(priskv_transport_client_impl *impl, uint64_t id, priskv_req_command cmd, priskv_generic_cb cb, priskv_sgl *sgl, uint16_t nsgl, const char *str, uint64_t timeout, priskv_memory **auto_mems)
{
    priskv_log_debug("pending_req_create: id %lu, cmd %d, cb %p, sgl %p, nsgl %d, str %s, timeout %lu\n", id, cmd, cb, sgl, nsgl, str, timeout);
    pending_req *req = malloc(sizeof(pending_req));
    if (!req) return NULL;

    req->id = id;
    req->cmd = cmd;
    req->cb = cb;
    req->sgl = sgl;
    req->nsgl = nsgl;
    req->str = str ? strdup(str) : NULL;
    req->timeout = timeout;
    req->auto_mems = auto_mems;
    impl->npending++;
    return req;
}

static void pending_req_destroy(priskv_transport_client_impl *impl, pending_req *req)
{
    priskv_log_debug("pending_req_destroy: id %lu\n", req->id);
    if (req->str) free(req->str);
    if (req->auto_mems) {
        for (uint16_t k = 0; k < req->nsgl; k++) {
            if (req->auto_mems[k]) priskv_dereg_memory(req->auto_mems[k]);
        }
        free(req->auto_mems);
    }
    impl->npending--;
}

static priskv_client *priskv_client_new(void)
{
    priskv_client *c = calloc(1, sizeof(*c));
    return c;
}

static void priskv_client_free(priskv_client *c)
{
    free(c);
}

priskv_client *priskv_connect(const char *raddr, int rport, const char *laddr, int lport, int nqueue)
{
    ucp_config_t *config;
    if (ucp_config_read(NULL, NULL, &config) != UCS_OK) {
        priskv_log_error("priskv_connect: read ucp config failed\n");
        return NULL;
    }

    ucp_params_t params;
    memset(&params, 0, sizeof(params));
    params.field_mask = UCP_PARAM_FIELD_FEATURES | UCP_PARAM_FIELD_REQUEST_SIZE | UCP_PARAM_FIELD_ESTIMATED_NUM_PPN;
    params.features = UCP_FEATURE_TAG | UCP_FEATURE_RMA | UCP_FEATURE_WAKEUP | UCP_FEATURE_AM;
    params.request_size = 0;
    params.estimated_num_ppn = 1;

    priskv_client *client = priskv_client_new();
    if (!client) {
        priskv_log_error("priskv_connect: create client failed\n");
        ucp_config_release(config);
        return NULL;
    }

    priskv_transport_client_impl *impl = calloc(1, sizeof(*impl));
    if (!impl) {
        priskv_client_free(client);
        ucp_config_release(config);
        return NULL;
    }

    if (ucp_init(&params, config, &impl->context) != UCS_OK) {
        priskv_log_error("priskv_connect: init ucp failed\n");
        free(impl);
        priskv_client_free(client);
        ucp_config_release(config);
        return NULL;
    }
    ucp_config_release(config);

    ucp_worker_params_t wparams;
    memset(&wparams, 0, sizeof(wparams));
    wparams.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    wparams.thread_mode = UCS_THREAD_MODE_SINGLE;
    if (ucp_worker_create(impl->context, &wparams, &impl->worker) != UCS_OK) {
        priskv_log_error("priskv_connect: create worker failed\n");
        ucp_cleanup(impl->context);
        free(impl);
        priskv_client_free(client);
        return NULL;
    }

    {
        ucp_am_handler_param_t hparam;
        memset(&hparam, 0, sizeof(hparam));
        hparam.field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID | UCP_AM_HANDLER_PARAM_FIELD_FLAGS | UCP_AM_HANDLER_PARAM_FIELD_CB | UCP_AM_HANDLER_PARAM_FIELD_ARG;
        hparam.id = priskv_transport_am_id_resp;
        hparam.flags = UCP_AM_FLAG_WHOLE_MSG;
        hparam.cb = priskv_transport_am_resp_cb;
        hparam.arg = impl;
        if (ucp_worker_set_am_recv_handler(impl->worker, &hparam) != UCS_OK) {
            priskv_log_error("priskv_connect: set am recv handler failed\n");
            ucp_worker_destroy(impl->worker);
            ucp_cleanup(impl->context);
            free(impl);
            priskv_client_free(client);
            return NULL;
        }
        memset(&hparam, 0, sizeof(hparam));
        hparam.field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID | UCP_AM_HANDLER_PARAM_FIELD_FLAGS | UCP_AM_HANDLER_PARAM_FIELD_CB | UCP_AM_HANDLER_PARAM_FIELD_ARG;
        hparam.id = priskv_transport_am_id_info_resp;
        hparam.flags = UCP_AM_FLAG_WHOLE_MSG;
        hparam.cb = priskv_transport_am_info_cb;
        hparam.arg = impl;
        if (ucp_worker_set_am_recv_handler(impl->worker, &hparam) != UCS_OK) {
            priskv_log_error("priskv_connect: set am recv handler failed\n");
            ucp_worker_destroy(impl->worker);
            ucp_cleanup(impl->context);
            free(impl);
            priskv_client_free(client);
            return NULL;
        }
    }

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(rport);
    inet_pton(AF_INET, raddr, &dst.sin_addr);

    ucp_ep_params_t ep_params;
    memset(&ep_params, 0, sizeof(ep_params));
    ep_params.field_mask = UCP_EP_PARAM_FIELD_FLAGS | UCP_EP_PARAM_FIELD_SOCK_ADDR | UCP_EP_PARAM_FIELD_ERR_HANDLER;
    ep_params.flags = UCP_EP_PARAMS_FLAGS_CLIENT_SERVER;
    ep_params.sockaddr.addr = (const struct sockaddr *)&dst;
    ep_params.sockaddr.addrlen = sizeof(dst);
    ep_params.err_handler.cb = priskv_client_ep_err_cb;
    ep_params.err_handler.arg = impl;
    if (ucp_ep_create(impl->worker, &ep_params, &impl->ep) != UCS_OK) {
        priskv_log_error("priskv_connect: create ep failed\n");
        ucp_worker_destroy(impl->worker);
        ucp_cleanup(impl->context);
        free(impl);
        priskv_client_free(client);
        return NULL;
    }

    impl->nqueue = nqueue;
    ucp_worker_get_efd(impl->worker, &impl->efd);
    {
        ucs_status_t st;
        do {
            ucp_worker_progress(impl->worker);
            st = ucp_worker_arm(impl->worker);
        } while (st == UCS_ERR_BUSY);
    }
    impl->owner = client;
    client->impl = impl;
    {
        ucp_request_param_t p;
        memset(&p, 0, sizeof(p));
        p.op_attr_mask = UCP_OP_ATTR_FIELD_MEMORY_TYPE | UCP_OP_ATTR_FIELD_FLAGS;
        p.memory_type = UCS_MEMORY_TYPE_HOST;
        p.flags = UCP_AM_SEND_FLAG_REPLY;
        void *r = ucp_am_send_nbx(impl->ep, priskv_transport_am_id_info_req, NULL, 0, NULL, 0, &p);
        if (UCS_PTR_IS_PTR(r)) {
            int spins = 1000;
            while (ucp_request_check_status(r) == UCS_INPROGRESS && spins-- > 0) {
                ucp_worker_progress(impl->worker);
            }
            ucp_request_free(r);
        }
    }
    return client;
}

void priskv_close(priskv_client *client)
{
    if (!client || !client->impl) return;
    priskv_transport_client_impl *impl = client->impl;
    if (impl->ep) {
        ucp_ep_destroy(impl->ep);
    }
    if (impl->worker) {
        ucp_worker_destroy(impl->worker);
    }
    if (impl->context) {
        ucp_cleanup(impl->context);
    }
    free(impl);
    priskv_client_free(client);
}

int priskv_get_fd(priskv_client *client)
{
    if (!client || !client->impl) return -1;
    return client->impl->efd;
}

int priskv_process(priskv_client *client, uint32_t event)
{
    if (!client || !client->impl) return -ENOTCONN;
    ucs_status_t st;
    do {
        ucp_worker_progress(client->impl->worker);
        st = ucp_worker_arm(client->impl->worker);
    } while (st == UCS_ERR_BUSY);
    return 0;
}

priskv_memory *priskv_reg_memory(priskv_client *client, uint64_t offset, size_t length, uint64_t iova, int fd)
{
    if (!client || !client->impl) return NULL;
    priskv_memory *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    priskv_transport_memory_impl *impl = calloc(1, sizeof(*impl));
    if (!impl) { free(m); return NULL; }

    ucp_mem_map_params_t mm;
    memset(&mm, 0, sizeof(mm));
    mm.field_mask = UCP_MEM_MAP_PARAM_FIELD_LENGTH | UCP_MEM_MAP_PARAM_FIELD_ADDRESS;
    mm.length = length;
    mm.address = (void *)(uintptr_t)offset;
    if (ucp_mem_map(client->impl->context, &mm, &impl->memh) != UCS_OK) {
        free(impl); free(m); return NULL;
    }
    void *rkey_buf = NULL; size_t rkey_len = 0;
    if (ucp_rkey_pack(client->impl->context, impl->memh, &rkey_buf, &rkey_len) != UCS_OK) {
        ucp_mem_unmap(client->impl->context, impl->memh);
        free(impl); free(m); return NULL;
    }
    impl->rkey_buf = rkey_buf;
    impl->rkey_len = rkey_len;
    impl->iova = iova;
    impl->length = length;
    impl->context = client->impl->context;
    m->impl = impl;
    return m;
}

void priskv_dereg_memory(priskv_memory *mem)
{
    if (!mem) return;
    priskv_transport_memory_impl *impl = mem->impl;
    if (impl) {
        if (impl->rkey_buf) ucp_rkey_buffer_release(impl->rkey_buf);
        if (impl->memh) ucp_mem_unmap(impl->context, impl->memh);
        free(impl);
    }
    free(mem);
}

static void priskv_client_send_done_cb(void *request, ucs_status_t status, void *user_data)
{
    if (user_data) free(user_data);
    if (request) ucp_request_free(request);
}

static int priskv_transport_send_am_req(priskv_client *client, const void *buf, size_t len)
{
    ucp_request_param_t p;
    memset(&p, 0, sizeof(p));
    p.op_attr_mask = UCP_OP_ATTR_FIELD_MEMORY_TYPE | UCP_OP_ATTR_FIELD_FLAGS | UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA;
    p.memory_type = UCS_MEMORY_TYPE_HOST;
    p.flags = UCP_AM_SEND_FLAG_REPLY;
    p.cb.send = priskv_client_send_done_cb;
    p.user_data = (void *)buf;
    void *r = ucp_am_send_nbx(client->impl->ep, priskv_transport_am_id_req, NULL, 0, buf, len, &p);
    if (UCS_PTR_IS_ERR(r)) { free((void *)buf); return -1; }
    if (!UCS_PTR_IS_PTR(r)) { free((void *)buf); }
    return 0;
}

static void *priskv_transport_build_req_buf(priskv_req_command cmd, const char *key, priskv_sgl *sgl, uint16_t nsgl, uint64_t timeout, pending_req *preq, size_t *out_len)
{
    uint16_t keylen = (uint16_t)strlen(key);
    size_t hdr = sizeof(priskv_request) + nsgl * sizeof(priskv_keyed_sgl) + keylen;
    size_t rks = 0;
    for (uint16_t i = 0; i < nsgl; i++) {
        rks += sizeof(uint16_t);
        rks += ((priskv_transport_memory_impl *)sgl[i].mem->impl)->rkey_len;
    }
    size_t total = hdr + rks;
    uint8_t *buf = malloc(total);
    if (!buf) return NULL;
    priskv_request *req = (priskv_request *)buf;
    uint64_t request_id = (uint64_t)preq;
    priskv_log_debug("priskv_transport_build_req_buf: cmd %d, key %.*s, nsgl %d, timeout %lu, request_id %lu\n", cmd, key, nsgl, timeout, request_id);

    req->request_id = htobe64(request_id);
    req->timeout = htobe64(timeout);
    req->command = htobe16(cmd);
    req->nsgl = htobe16(nsgl);
    req->key_length = htobe16(keylen);
    for (uint16_t i = 0; i < nsgl; i++) {
        req->sgls[i].addr = htobe64(sgl[i].iova);
        req->sgls[i].length = htobe32(sgl[i].length);
        req->sgls[i].key = htobe32(0);
    }
    uint8_t *keyptr = priskv_request_key(req, nsgl);
    memcpy(keyptr, key, keylen);
    uint8_t *p = keyptr + keylen;
    for (uint16_t i = 0; i < nsgl; i++) {
        priskv_transport_memory_impl *minfo = (priskv_transport_memory_impl *)sgl[i].mem->impl;
        uint16_t l = (uint16_t)minfo->rkey_len;
        uint16_t lbe = htobe16(l);
        memcpy(p, &lbe, sizeof(lbe));
        p += sizeof(lbe);
        memcpy(p, minfo->rkey_buf, l);
        p += l;
    }
    *out_len = total;
    return buf;
}

static int submit_req(priskv_client *client, priskv_req_command cmd, const char *str,
                      priskv_sgl *sgl, uint16_t nsgl, uint64_t timeout,
                      uint64_t request_id, priskv_generic_cb cb)
{
    size_t len = 0;
    priskv_memory **auto_mems = NULL;
    if (nsgl > 0) {
        auto_mems = calloc(nsgl, sizeof(priskv_memory *));
        for (uint16_t i = 0; i < nsgl; i++) {
            if (!sgl[i].mem) {
                priskv_memory *m = priskv_reg_memory(client, sgl[i].iova, sgl[i].length, sgl[i].iova, -1);
                auto_mems[i] = m;
                sgl[i].mem = m;
            }
        }
    }
    pending_req *req = pending_req_create(client->impl, request_id, cmd, cb, sgl, nsgl, str, timeout, auto_mems);
    if (!req) return -ENOMEM;
    void *buf = priskv_transport_build_req_buf(cmd, str, sgl, nsgl, timeout, req, &len);
    if (!buf) return -ENOMEM;
    return priskv_transport_send_am_req(client, buf, len);
}

static int ensure_keys_buffer(priskv_transport_client_impl *impl, priskv_client *client)
{
    if (impl->keys_buf && impl->keys_mem) return 0;
    impl->keys_buf_len = 4096;
    impl->keys_buf = malloc(impl->keys_buf_len);
    if (!impl->keys_buf) return -ENOMEM;
    impl->keys_mem = priskv_reg_memory(client, (uint64_t)impl->keys_buf, impl->keys_buf_len,
                                       (uint64_t)impl->keys_buf, -1);
    return impl->keys_mem ? 0 : -ENOMEM;
}

int priskv_get_async(priskv_client *client, const char *key, priskv_sgl *sgl, uint16_t nsgl,
                   uint64_t request_id, priskv_generic_cb cb)
{
    if (!client) return -EINVAL;
    return submit_req(client, PRISKV_COMMAND_GET, key, sgl, nsgl, 0, request_id, cb);
}

int priskv_set_async(priskv_client *client, const char *key, priskv_sgl *sgl, uint16_t nsgl,
                   uint64_t timeout, uint64_t request_id, priskv_generic_cb cb)
{
    if (!client) return -EINVAL;
    return submit_req(client, PRISKV_COMMAND_SET, key, sgl, nsgl, timeout, request_id, cb);
}

int priskv_test_async(priskv_client *client, const char *key, uint64_t request_id, priskv_generic_cb cb)
{
    if (!client) return -EINVAL;
    return submit_req(client, PRISKV_COMMAND_TEST, key, NULL, 0, 0, request_id, cb);
}

int priskv_delete_async(priskv_client *client, const char *key, uint64_t request_id, priskv_generic_cb cb)
{
    if (!client) return -EINVAL;
    return submit_req(client, PRISKV_COMMAND_DELETE, key, NULL, 0, 0, request_id, cb);
}

int priskv_expire_async(priskv_client *client, const char *key, uint64_t timeout, uint64_t request_id, priskv_generic_cb cb)
{
    if (!client) return -EINVAL;
    return submit_req(client, PRISKV_COMMAND_EXPIRE, key, NULL, 0, timeout, request_id, cb);
}

int priskv_nrkeys_async(priskv_client *client, const char *regex, uint64_t request_id, priskv_generic_cb cb)
{
    if (!client) return -EINVAL;
    return submit_req(client, PRISKV_COMMAND_NRKEYS, regex, NULL, 0, 0, request_id, cb);
}

int priskv_flush_async(priskv_client *client, const char *regex, uint64_t request_id, priskv_generic_cb cb)
{
    if (!client) return -EINVAL;
    return submit_req(client, PRISKV_COMMAND_FLUSH, regex, NULL, 0, 0, request_id, cb);
}

int priskv_keys_async(priskv_client *client, const char *regex, uint64_t request_id, priskv_generic_cb cb)
{
    if (!client) return -EINVAL;
    priskv_transport_client_impl *impl = client->impl;
    int e = ensure_keys_buffer(impl, client);
    if (e) return e;
    priskv_sgl sgl;
    sgl.iova = (uint64_t)impl->keys_buf;
    sgl.length = impl->keys_buf_len;
    sgl.mem = impl->keys_mem;
    return submit_req(client, PRISKV_COMMAND_KEYS, regex, &sgl, 1, 0, request_id, cb);
}

void priskv_keyset_free(priskv_keyset *keyset)
{
    if (!keyset) return;
    for (uint32_t i = 0; i < keyset->nkey; i++) {
        if (keyset->keys[i].key) free(keyset->keys[i].key);
    }
    if (keyset->keys) free(keyset->keys);
    free(keyset);
}

uint64_t priskv_capacity(priskv_client *client)
{
    if (!client || !client->impl) return 0;
    return client->impl->capacity;
}

static ucs_status_t priskv_transport_am_info_cb(void *arg, const void *header, size_t header_length, void *data, size_t length, const ucp_am_recv_param_t *param)
{
    priskv_transport_client_impl *impl = (priskv_transport_client_impl *)arg;
    uint32_t recv_attr = param->recv_attr;
    int is_rndv = !!(recv_attr & UCP_AM_RECV_ATTR_FLAG_RNDV);
    uint8_t *msg = (uint8_t *)data;
    uint8_t *owned_buf = NULL;
    if (is_rndv) {
        priskv_log_debug("priskv_transport_am_info_cb: AM recv rndv data, length %zu\n", length);
        owned_buf = (uint8_t *)malloc(length);
        if (!owned_buf) return UCS_OK;
        ucp_request_param_t rp;
        memset(&rp, 0, sizeof(rp));
        rp.op_attr_mask = UCP_OP_ATTR_FIELD_MEMORY_TYPE;
        rp.memory_type = UCS_MEMORY_TYPE_HOST;
        void *r = ucp_am_recv_data_nbx(impl->worker, data, owned_buf, length, &rp);
        if (UCS_PTR_IS_PTR(r)) {
            while (ucp_request_check_status(r) == UCS_INPROGRESS) {}
            ucp_request_free(r);
        }
        msg = owned_buf;
    }
    struct {
        uint64_t capacity_be;
        uint16_t max_sgl_be;
        uint16_t max_key_length_be;
        uint16_t max_inflight_command_be;
    } *info = (void *)msg;
    impl->capacity = be64toh(info->capacity_be);
    impl->max_sgl = be16toh(info->max_sgl_be);
    impl->max_key_length = be16toh(info->max_key_length_be);
    impl->max_inflight_command = be16toh(info->max_inflight_command_be);
    priskv_log_info("priskv_transport_am_info_cb: capacity %" PRIu64 ", max_sgl %" PRIu16 ", max_key_length %" PRIu16 ", max_inflight_command %" PRIu16 "\n",
                    impl->capacity, impl->max_sgl, impl->max_key_length, impl->max_inflight_command);
    if (is_rndv) {
        ucp_am_data_release(impl->worker, data);
    }
    if (owned_buf) free(owned_buf);
    return UCS_OK;
}

static ucs_status_t priskv_transport_am_resp_cb(void *arg, const void *header, size_t header_length, void *data, size_t length, const ucp_am_recv_param_t *param)
{
    priskv_log_debug("priskv_transport_am_resp_cb: recv am resp, length %zu\n", length);
    priskv_transport_client_impl *impl = (priskv_transport_client_impl *)arg;
    uint32_t recv_attr = param->recv_attr;
    int is_rndv = !!(recv_attr & UCP_AM_RECV_ATTR_FLAG_RNDV);
    uint8_t *msg = (uint8_t *)data;
    uint8_t *owned_buf = NULL;
    if (is_rndv) {
        priskv_log_debug("priskv_transport_am_resp_cb: recv am resp rndv, length %zu\n", length);
        owned_buf = (uint8_t *)malloc(length);
        if (!owned_buf) {
            priskv_log_error("priskv_transport_am_resp_cb: recv am resp rndv, malloc failed\n");
            return UCS_OK;
        }
        ucp_request_param_t rp;
        memset(&rp, 0, sizeof(rp));
        rp.op_attr_mask = UCP_OP_ATTR_FIELD_MEMORY_TYPE;
        rp.memory_type = UCS_MEMORY_TYPE_HOST;
        void *r = ucp_am_recv_data_nbx(impl->worker, data, owned_buf, length, &rp);
        if (UCS_PTR_IS_PTR(r)) {
            while (ucp_request_check_status(r) == UCS_INPROGRESS) {
            }
            ucp_request_free(r);
        }
        msg = owned_buf;
    }
    priskv_response *resp = (priskv_response *)msg;
    uint64_t id = be64toh(resp->request_id);
    uint16_t status = be16toh(resp->status);
    uint32_t len = be32toh(resp->length);
    priskv_log_debug("priskv_transport_am_resp_cb: recv am resp, id %lu, status %s, length %u\n", id, priskv_resp_status_str(status), len);

    pending_req *p = (pending_req *)id;
    if (p->cb) {
        if (p->cmd == PRISKV_COMMAND_KEYS) {
            if (status == PRISKV_RESP_STATUS_VALUE_TOO_BIG) {
                size_t newlen = len + len / 8;
                if (impl->keys_mem) {
                    priskv_dereg_memory(impl->keys_mem);
                    impl->keys_mem = NULL;
                }
                if (impl->keys_buf) free(impl->keys_buf);
                impl->keys_buf = malloc(newlen);
                impl->keys_buf_len = newlen;
                impl->keys_mem = priskv_reg_memory(impl->owner, (uint64_t)impl->keys_buf, newlen, (uint64_t)impl->keys_buf, -1);
                priskv_sgl sgl;
                sgl.iova = (uint64_t)impl->keys_buf;
                sgl.length = newlen;
                sgl.mem = impl->keys_mem;
                size_t slen = 0;
                void *buf = priskv_transport_build_req_buf(PRISKV_COMMAND_KEYS, p->str, &sgl, 1, 0, id, &slen);
                if (buf) {
                    priskv_transport_send_am_req(impl->owner, buf, slen);
                }
            } else if (status == PRISKV_RESP_STATUS_OK) {
                priskv_keyset *keyset = calloc(1, sizeof(priskv_keyset));
                uint8_t *pbuf = (uint8_t *)impl->keys_buf;
                uint32_t nkey = 0;
                uint32_t off = 0;
                while (off + sizeof(priskv_keys_resp) <= len) {
                    priskv_keys_resp *kr = (priskv_keys_resp *)(pbuf + off);
                    uint16_t klen = kr->keylen;
                    off += sizeof(priskv_keys_resp);
                    if (off + klen > len) break;
                    nkey++;
                    off += klen;
                }
                keyset->nkey = nkey;
                keyset->keys = calloc(nkey, sizeof(priskv_key));
                off = 0;
                for (uint32_t i = 0; i < nkey; i++) {
                    priskv_keys_resp *kr = (priskv_keys_resp *)(pbuf + off);
                    uint16_t klen = kr->keylen;
                    uint32_t vlen = kr->valuelen;
                    off += sizeof(priskv_keys_resp);
                    keyset->keys[i].key = malloc(klen + 1);
                    memcpy(keyset->keys[i].key, pbuf + off, klen);
                    keyset->keys[i].key[klen] = '\0';
                    keyset->keys[i].valuelen = vlen;
                    off += klen;
                }
                p->cb(id, (priskv_status)status, keyset);
            } else {
                p->cb(id, (priskv_status)status, NULL);
            }
        } else {
            p->cb(id, (priskv_status)status, &len);
        }
    }
    pending_req_destroy(impl, p);
    if (is_rndv) {
        ucp_am_data_release(impl->worker, data);
    }
    if (owned_buf) free(owned_buf);
    return UCS_OK;
}

static void priskv_client_ep_err_cb(void *arg, ucp_ep_h ep, ucs_status_t status)
{
    priskv_transport_client_impl *impl = (priskv_transport_client_impl *)arg;
    ucp_request_param_t p;
    memset(&p, 0, sizeof(p));
    p.op_attr_mask = UCP_OP_ATTR_FIELD_FLAGS;
    p.flags = UCP_EP_CLOSE_FLAG_FORCE;
    void *req = ucp_ep_close_nbx(ep, &p);
    if (UCS_PTR_IS_PTR(req)) {
        while (ucp_request_check_status(req) == UCS_INPROGRESS) {}
        ucp_request_free(req);
    }
    (void)impl;
}
