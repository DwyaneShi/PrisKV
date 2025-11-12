// Copyright (c) 2025 ByteDance Ltd. and/or its affiliates
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*
 * Authors:
 *   Jinlong Xuan <15563983051@163.com>
 *   Xu Ji <sov.matrixac@gmail.com>
 *   Yu Wang <wangyu.steph@bytedance.com>
 *   Bo Liu <liubo.2024@bytedance.com>
 *   Zhenwei Pi <pizhenwei@bytedance.com>
 *   Rui Zhang <zhangrui.1203@bytedance.com>
 *   Changqi Lu <luchangqi.123@bytedance.com>
 *   Enhua Zhou <zhouenhua@bytedance.com>
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>

#include "evhtp.h"
#include "priskv-log.h"
#include "priskv-version.h"
#include "http.h"
#include "priskv-codec.h"
#include "jsonobjs.h"
#include "info.h"
#include "acl.h"
#include "kvmanage.h"

static priskv_codec *codec;

typedef struct priskv_http_api_handler {
    const char *path;
    void (*handler)(evhtp_request_t *req, void *arg);
} priskv_http_api_handler;

static char *priskv_http_api_get_body(evhtp_request_t *req)
{
    const char *content_length_str = evhtp_kv_find(req->headers_in, "Content-Length");
    if (!content_length_str) {
        return NULL;
    }

    int content_length = atoi(content_length_str);
    if (content_length <= 0) {
        return NULL;
    }

    char *body = malloc(content_length + 1);
    if (!body) {
        return NULL;
    }

    int len = 0;
    while (len < content_length) {
        len += evbuffer_remove(req->buffer_in, body + len, content_length - len);
    }

    body[content_length] = '\0';
    return body;
}

static void priskv_http_api_default_cb(evhtp_request_t *req, void *arg)
{
    evhtp_send_reply(req, EVHTP_RES_NOTFOUND);
}

static int priskv_http_acl_add(evhtp_request_t *req)
{
    priskv_acl_info *info = NULL;
    int status = 0;
    char *body = NULL;

    if (evhtp_request_get_method(req) != htp_method_POST) {
        return EVHTP_RES_METHNALLOWED;
    }

    body = priskv_http_api_get_body(req);
    if (!body) {
        return EVHTP_RES_BADREQ;
    }

    info = priskv_codec_decode(codec, body, &priskv_acl_info_obj);
    if (!info) {
        status = EVHTP_RES_BADREQ;
    }

    if (!status) {
        for (int i = 0; i < info->nrules; i++) {
            if (priskv_acl_add(info->rules[i])) {
                status = EVHTP_RES_BADREQ;
            }
        }
    }

    priskv_codec_free_struct(codec, info, &priskv_acl_info_obj);
    free(body);

    return status;
}

static int priskv_http_acl_del(evhtp_request_t *req)
{
    priskv_acl_info *info = NULL;
    int status = 0;
    char *body = NULL;

    if (evhtp_request_get_method(req) != htp_method_POST) {
        return EVHTP_RES_METHNALLOWED;
    }

    body = priskv_http_api_get_body(req);
    if (!body) {
        return EVHTP_RES_BADREQ;
    }

    info = priskv_codec_decode(codec, body, &priskv_acl_info_obj);
    if (!info) {
        status = EVHTP_RES_BADREQ;
    }

    if (!status) {
        for (int i = 0; i < info->nrules; i++) {
            if (priskv_acl_del(info->rules[i])) {
                status = EVHTP_RES_BADREQ;
            }
        }
    }

    priskv_codec_free_struct(codec, info, &priskv_acl_info_obj);
    free(body);

    return status;
}

static int priskv_http_acl_list(evhtp_request_t *req)
{
    priskv_acl_info info;
    int status = 0;

    if (evhtp_request_get_method(req) != htp_method_GET) {
        return EVHTP_RES_METHNALLOWED;
    }

    info.rules = priskv_acl_get_rules(&info.nrules);
    if (!info.rules) {
        return EVHTP_RES_SERVERR;
    }

    char *str = priskv_codec_code(codec, &info, &priskv_acl_info_obj);
    if (!str) {
        priskv_log_error("failed to encode response: %s\n", priskv_codec_get_error(codec));
        status = EVHTP_RES_SERVERR;
    } else {
        evbuffer_add(req->buffer_out, str, strlen(str));
        evhtp_headers_add_header(req->headers_out,
                                 evhtp_header_new("Content-Type", "application/json", 0, 0));
        free(str);
    }

    priskv_acl_free_rules(info.rules, info.nrules);

    return status;
}

typedef struct priskv_http_acl_action {
    const char *action;
    int (*handler)(evhtp_request_t *req);
} priskv_http_acl_action;

static priskv_http_acl_action priskv_http_acl_actions[] = {
    {"add", priskv_http_acl_add},
    {"del", priskv_http_acl_del},
    {"list", priskv_http_acl_list},
};

static void priskv_http_api_acl_cb(evhtp_request_t *req, void *arg)
{
    evhtp_kv_t *kv;

    if (strcmp(req->uri->path->full, "/api/acl")) {
        evhtp_send_reply(req, EVHTP_RES_NOTFOUND);
        return;
    }

    if (req->uri->query) {
        TAILQ_FOREACH(kv, req->uri->query, next)
        {
            if (strcmp(kv->key, "action")) {
                evhtp_send_reply(req, EVHTP_RES_BADREQ);
                return;
            }

            for (int i = 0; i < sizeof(priskv_http_acl_actions) / sizeof(priskv_http_acl_action); i++) {
                if (!strcmp(kv->val, priskv_http_acl_actions[i].action)) {
                    priskv_http_acl_action *action = &priskv_http_acl_actions[i];
                    int status = action->handler(req);
                    if (status) {
                        evhtp_send_reply(req, status);
                        return;
                    }
                    evhtp_send_reply(req, EVHTP_RES_OK);
                    return;
                }
            }
        }
    }

    evhtp_send_reply(req, EVHTP_RES_BADREQ);
}

static void priskv_http_api_ping_cb(evhtp_request_t *req, void *arg)
{
    if (strcmp(req->uri->path->full, "/api/ping")) {
        evhtp_send_reply(req, EVHTP_RES_NOTFOUND);
        return;
    }

    if (evhtp_request_get_method(req) != htp_method_GET) {
        evhtp_send_reply(req, EVHTP_RES_METHNALLOWED);
        return;
    }

    evbuffer_add(req->buffer_out, "pong", 4);

    evhtp_send_reply(req, EVHTP_RES_OK);
}

static void priskv_http_api_version_cb(evhtp_request_t *req, void *arg)
{
    priskv_version_response resp = {
        .version = priskv_get_version(),
    };

    if (strcmp(req->uri->path->full, "/api/version")) {
        evhtp_send_reply(req, EVHTP_RES_NOTFOUND);
        return;
    }

    if (evhtp_request_get_method(req) != htp_method_GET) {
        evhtp_send_reply(req, EVHTP_RES_METHNALLOWED);
        return;
    }

    char *str = priskv_codec_code(codec, &resp, &priskv_version_response_obj);
    if (!str) {
        priskv_log_error("failed to encode response: %s\n", priskv_codec_get_error(codec));
        evhtp_send_reply(req, EVHTP_RES_SERVERR);
        return;
    }
    evbuffer_add(req->buffer_out, str, strlen(str));
    free(str);

    evhtp_headers_add_header(req->headers_out,
                             evhtp_header_new("Content-Type", "application/json", 0, 0));
    evhtp_send_reply(req, EVHTP_RES_OK);
}

static void priskv_http_api_info_cb(evhtp_request_t *req, void *arg)
{
    evhtp_kv_t *kv;
    const char *items[64] = {0};
    int nitems = 0;
    char *data = NULL;

    if (evhtp_request_get_method(req) != htp_method_GET) {
        evhtp_send_reply(req, EVHTP_RES_METHNALLOWED);
        return;
    }

    if (strcmp(req->uri->path->full, "/api/info")) {
        evhtp_send_reply(req, EVHTP_RES_NOTFOUND);
        return;
    }

    if (req->uri->query) {
        TAILQ_FOREACH(kv, req->uri->query, next)
        {
            if (strcmp(kv->key, "item")) {
                evhtp_send_reply(req, EVHTP_RES_BADREQ);
                return;
            }

            items[nitems++] = kv->val;
            if (nitems >= 64) {
                break;
            }
        }
    }

    if (!priskv_info_items_available(items, nitems)) {
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    data = priskv_info_json(codec, items, nitems);
    evbuffer_add(req->buffer_out, data, strlen(data));
    free(data);

    evhtp_headers_add_header(req->headers_out,
                             evhtp_header_new("Content-Type", "application/json", 0, 0));
    evhtp_send_reply(req, EVHTP_RES_OK);
}

typedef struct priskv_kvmanage_context {
    evhtp_request_t *req;
    priskv_kvmanage_info *info;
} priskv_kvmanage_context;

static void priskv_http_api_kvmanage_bottom(void *arg, int status)
{
    priskv_kvmanage_context *ctx = arg;
    evhtp_request_t *req = ctx->req;
    evhtp_res code = status ? EVHTP_RES_BADREQ : EVHTP_RES_200;

    priskv_codec_free_struct(codec, ctx->info, &priskv_kvmanage_info_obj);

    evhtp_request_resume(req);

    evhtp_send_reply(req, code);

    free(ctx);
}

static void priskv_http_api_kvmanage(evhtp_request_t *req, priskv_kvmanage_action action)
{
    priskv_kvmanage_context *ctx = NULL;
    priskv_kvmanage_info *info = NULL;

    char *body = NULL;

    if (evhtp_request_get_method(req) != htp_method_POST) {
        evhtp_send_reply(req, EVHTP_RES_METHNALLOWED);
        return;
    }

    body = priskv_http_api_get_body(req);
    if (!body) {
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    info = priskv_codec_decode(codec, body, &priskv_kvmanage_info_obj);
    if (!info) {
        goto err;
    }

    if (!info->addr || !strlen(info->addr) || !info->port || !info->nkeys || !info->keys) {
        goto err;
    }

    ctx = calloc(1, sizeof(priskv_kvmanage_context));
    ctx->req = req;
    ctx->info = info;

    if (action(info->addr, info->port, info->keys, info->nkeys, req->htp->evbase,
               priskv_http_api_kvmanage_bottom, ctx) < 0) {
        goto err;
    }

    free(body);

    evhtp_request_pause(req);
    return;

err:
    free(body);
    priskv_codec_free_struct(codec, info, &priskv_kvmanage_info_obj);
    free(ctx);
    evhtp_send_reply(req, EVHTP_RES_BADREQ);
}

static void priskv_http_api_kvcopy_cb(evhtp_request_t *req, void *arg)
{
    priskv_http_api_kvmanage(req, priskv_kvmanage_copy_to);
}

static void priskv_http_api_kvmove_cb(evhtp_request_t *req, void *arg)
{
    priskv_http_api_kvmanage(req, priskv_kvmanage_move_to);
}

static priskv_http_api_handler api_handlers[] = {
    /* Sort by path from a to z */
    {"/api/acl", priskv_http_api_acl_cb},       {"/api/info", priskv_http_api_info_cb},
    {"/api/kvcopy", priskv_http_api_kvcopy_cb}, {"/api/kvmove", priskv_http_api_kvmove_cb},
    {"/api/ping", priskv_http_api_ping_cb},     {"/api/version", priskv_http_api_version_cb},
    {"/api", priskv_http_api_default_cb}, /* MUST be at the end */
};

static int priskv_verify2opts(const char *opts_str)
{
    if (!opts_str || !strcasecmp(opts_str, "off")) {
        return SSL_VERIFY_NONE;
    }

    if (!strcasecmp(opts_str, "optional")) {
        return SSL_VERIFY_PEER;
    }

    if (!strcasecmp(opts_str, "on")) {
        return SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
    }

    return -1;
}

static evhtp_ssl_cfg_t *priskv_new_ssl_cfg(const char *cert, const char *key, const char *ca,
                                         const char *verify_client)
{
    evhtp_ssl_cfg_t *cfg = NULL;
    struct stat f_stat;

    if (stat(cert, &f_stat) < 0) {
        priskv_log_error("HTTP server: Cannot load SSL cert '%s' (%s)\n", cert, strerror(errno));
        return NULL;
    }

    if (stat(key, &f_stat) < 0) {
        priskv_log_error("HTTP server: Cannot load SSL key '%s' (%s)\n", key, strerror(errno));
        return NULL;
    }

    if (ca) {
        if (stat(ca, &f_stat) < 0) {
            priskv_log_error("HTTP server: Cannot find SSL CA File '%s' (%s)\n", ca, strerror(errno));
            return NULL;
        }
    }

    cfg = calloc(1, sizeof(evhtp_ssl_cfg_t));

    cfg->pemfile = strdup(cert);
    cfg->privfile = strdup(key);
    if (ca) {
        cfg->cafile = strdup(ca);
    }
    cfg->verify_peer = priskv_verify2opts(verify_client);
    cfg->verify_depth = 2;

    return cfg;
}

int priskv_http_start(struct event_base *base, priskv_http_config *config)
{
    struct addrinfo hints;
    struct addrinfo *res, *rp;
    char _port[6]; /* strlen("65535"); */
    int ret = 0;
    evhtp_t *http = evhtp_new(base, NULL);
    evhtp_ssl_cfg_t *ssl_cfg;

    if (!config->addr) {
        priskv_log_error("HTTP server: invalid address\n");
        return -1;
    }

    if (config->cert && config->key) {
        ssl_cfg = priskv_new_ssl_cfg(config->cert, config->key, config->ca, config->verify_client);
        if (ssl_cfg == NULL) {
            return -1;
        }

        if (evhtp_ssl_init(http, ssl_cfg) < 0) {
            priskv_log_error("HTTP server: failed to init SSL\n");
            return -1;
        }
        priskv_log_notice("HTTP server: using SSL with cert (%s), key (%s), ca (%s)\n", config->cert,
                        config->key, config->ca);
    }

    evhtp_set_gencb(http, priskv_http_api_default_cb, NULL);

    for (size_t i = 0; i < sizeof(api_handlers) / sizeof(priskv_http_api_handler); i++) {
        evhtp_set_cb(http, api_handlers[i].path, api_handlers[i].handler, NULL);
    }

    snprintf(_port, 6, "%d", config->port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    ret = getaddrinfo(config->addr, _port, &hints, &res);
    if (ret) {
        priskv_log_error("HTTP server: failed to getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        if (rp->ai_family != AF_UNSPEC) {
            if (evhtp_bind_sockaddr(http, rp->ai_addr, rp->ai_addrlen, 128) < 0) {
                priskv_log_error("HTTP server: failed to bind socket\n");
                return -1;
            }
            break;
        }
    }

    if (!rp) {
        priskv_log_error("HTTP server: failed to bind socket\n");
        return -1;
    }

    if (rp->ai_family == AF_INET) {
        priskv_log_notice("HTTP server: listening on %s:%d\n", config->addr, config->port);
    } else if (rp->ai_family == AF_INET6) {
        priskv_log_notice("HTTP server: listening on [%s]:%d\n", config->addr, config->port);
    }

    codec = priskv_codec_new();

    freeaddrinfo(res);

    return 0;
}
