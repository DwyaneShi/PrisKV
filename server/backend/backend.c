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
 *   Bo Liu <liubo.2024@bytedance.com>
 *   Jinlong Xuan <15563983051@163.com>
 *   Xu Ji <sov.matrixac@gmail.com>
 *   Yu Wang <wangyu.steph@bytedance.com>
 *   Zhenwei Pi <pizhenwei@bytedance.com>
 *   Rui Zhang <zhangrui.1203@bytedance.com>
 *   Changqi Lu <luchangqi.123@bytedance.com>
 *   Enhua Zhou <zhouenhua@bytedance.com>
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "list.h"
#include "backend.h"
#include "priskv-log.h"

static struct list_head global_bdrv_list = LIST_HEAD_INIT(global_bdrv_list);

void priskv_backend_register(priskv_backend_driver *bdrv)
{
    priskv_backend_driver *tmp;

    assert(bdrv->name);
    list_for_each (&global_bdrv_list, tmp, node) {
        assert(strcmp(tmp->name, bdrv->name));
    }

    list_add_tail(&global_bdrv_list, &bdrv->node);
}

static priskv_backend_driver *priskv_backend_find_driver(const char *name)
{
    priskv_backend_driver *_bdrv;

    list_for_each (&global_bdrv_list, _bdrv, node) {
        if (!strcmp(_bdrv->name, name)) {
            return _bdrv;
        }
    }

    return NULL;
}

static void free_link(priskv_backend_link *link);
static int parse_link(const char *address, priskv_backend_link *link)
{
    if (!address || *address == '\0') {
        return -EINVAL;
    }

    const char *delim = strchr(address, ':');
    if (!delim) {
        return -EINVAL;
    }

    link->protocol = strndup(address, delim - address);

    const char *end = strchr(address, ';');
    if (!end) {
        link->address = strdup(delim + 1);
        link->childaddr = NULL;
    } else {
        link->address = strndup(delim + 1, end - delim - 1);
        link->childaddr = strdup(end + 1);
    }

    if (!strlen(link->protocol) || !strlen(link->address) ||
        (link->childaddr && !strlen(link->childaddr))) {
        free_link(link);
        return -EINVAL;
    }

    return 0;
}

static void free_link(priskv_backend_link *link)
{
    free(link->protocol);
    free(link->address);
    free(link->childaddr);
}

priskv_backend_device *priskv_backend_open(const char *address, int epollfd)
{
    priskv_backend_device *bdev = calloc(1, sizeof(priskv_backend_device));

    if (parse_link(address, &bdev->link)) {
        free(bdev);
        return NULL;
    }

    bdev->epollfd = epollfd;

    bdev->bdrv = priskv_backend_find_driver(bdev->link.protocol);
    if (!bdev->bdrv) {
        free(bdev);
        return NULL;
    }

    if (bdev->bdrv->open(bdev)) {
        free(bdev);
        return NULL;
    }

    if (bdev->link.childaddr) {
        bdev->child = priskv_backend_open(bdev->link.childaddr, epollfd);
        if (!bdev->child) {
            bdev->bdrv->close(bdev);
            free(bdev);
            return NULL;
        }

        if (bdev->bdrv->clearup(bdev)) {
            priskv_log_error("BACKEND: clearup device(%s) failed\n", bdev->link.address);
            priskv_backend_close(bdev);
            return NULL;
        }
    }

    return bdev;
}

int priskv_backend_close(priskv_backend_device *bdev)
{
    int ret = 0;

    if (bdev->child) {
        ret = priskv_backend_close(bdev->child);
        if (ret) {
            return ret;
        }
    }

    ret = bdev->bdrv->close(bdev);

    free_link(&bdev->link);
    free(bdev);

    return ret;
}

typedef struct priskv_backend_freeup_context {
    priskv_backend_device *bdev;
    uint64_t valuelen;
    priskv_backend_driver_cb cb;
    void *cbarg;
} priskv_backend_freeup_context;

static void _priskv_backend_freeup(priskv_backend_freeup_context *ctx);
static void priskv_backend_freeup_cb(priskv_backend_status status, uint32_t valuelen, void *arg)
{
    priskv_backend_freeup_context *ctx = arg;

    if (status != PRISKV_BACKEND_STATUS_OK) {
        ctx->cb(status, ctx->valuelen, ctx->cbarg);
        free(ctx);
        return;
    }

    _priskv_backend_freeup(ctx);
}

static void _priskv_backend_freeup(priskv_backend_freeup_context *ctx)
{
    priskv_backend_device *bdev = ctx->bdev;
    uint64_t valuelen = ctx->valuelen;

    if (!bdev->bdrv->is_cacheable(bdev, valuelen)) {
        bdev->bdrv->evict(bdev, priskv_backend_freeup_cb, ctx);
        return;
    }

    ctx->cb(PRISKV_BACKEND_STATUS_OK, valuelen, ctx->cbarg);
    free(ctx);
}

static void priskv_backend_freeup(priskv_backend_device *bdev, uint64_t valuelen,
                                priskv_backend_driver_cb cb, void *cbarg)
{
    priskv_backend_freeup_context *ctx = calloc(1, sizeof(priskv_backend_freeup_context));
    ctx->bdev = bdev;
    ctx->valuelen = valuelen;
    ctx->cb = cb;
    ctx->cbarg = cbarg;

    _priskv_backend_freeup(ctx);
}

typedef enum priskv_backend_request_type {
    PRISKV_BACKEND_REQUEST_GET = 0,
    PRISKV_BACKEND_REQUEST_SET,
    PRISKV_BACKEND_REQUEST_DEL,
    PRISKV_BACKEND_REQUEST_TEST,
    PRISKV_BACKEND_REQUEST_MAX,
} priskv_backend_request_type;

typedef struct priskv_backend_request {
    priskv_backend_device *bdev;
    priskv_backend_request_type type;
    const char *key;
    uint8_t *val;
    uint64_t valuelen;
    uint64_t timeout;
    priskv_backend_driver_cb cb;
    void *cbarg;
} priskv_backend_request;

static void priskv_backend_cache_cb(priskv_backend_status status, uint32_t length, void *arg)
{
    priskv_backend_request *req = arg;

    if (status != PRISKV_BACKEND_STATUS_OK) {
        priskv_log_warn("BACKEND: cache failed, key: %s, status: %d\n", req->key, status);
    }

    req->cb(PRISKV_BACKEND_STATUS_OK, length, req->cbarg);
    free(req);
}

static void priskv_backend_cache(priskv_backend_status status, uint32_t length, void *arg)
{
    priskv_backend_request *req = arg;
    priskv_backend_device *bdev = req->bdev;

    if (status != PRISKV_BACKEND_STATUS_OK) {
        priskv_log_warn("BACKEND: cache failed, key: %s, status: %d\n", req->key, status);
        req->cb(PRISKV_BACKEND_STATUS_OK, length, req->cbarg);
        free(req);
        return;
    }

    bdev->bdrv->set(bdev, req->key, req->val, req->valuelen, 0, priskv_backend_cache_cb, req);
}

static void priskv_backend_child_get_cb(priskv_backend_status status, uint32_t length, void *arg)
{
    priskv_backend_request *req = arg;
    priskv_backend_device *bdev = req->bdev;

    if (status == PRISKV_BACKEND_STATUS_OK) {
        priskv_backend_freeup(bdev, length, priskv_backend_cache, req);
    } else {
        req->cb(status, length, req->cbarg);
        free(req);
    }
}

static void priskv_backend_get_cb(priskv_backend_status status, uint32_t length, void *arg)
{
    priskv_backend_request *req = arg;
    priskv_backend_device *bdev = req->bdev;

    if (status == PRISKV_BACKEND_STATUS_OK) {
        req->cb(status, length, req->cbarg);
        free(req);
        return;
    }

    if (bdev->child) {
        priskv_backend_get(bdev->child, req->key, req->val, req->valuelen, priskv_backend_child_get_cb,
                         req);
    } else {
        req->cb(status, length, req->cbarg);
        free(req);
    }
}

void priskv_backend_get(priskv_backend_device *bdev, const char *key, uint8_t *val, uint64_t valuelen,
                      priskv_backend_driver_cb cb, void *cbarg)
{
    priskv_backend_request *req = calloc(1, sizeof(priskv_backend_request));

    req->bdev = bdev;
    req->type = PRISKV_BACKEND_REQUEST_GET;
    req->key = key;
    req->val = val;
    req->valuelen = valuelen;
    req->cb = cb;
    req->cbarg = cbarg;

    bdev->bdrv->get(bdev, key, val, valuelen, priskv_backend_get_cb, req);
}

static void priskv_backend_set_cb(priskv_backend_status status, uint32_t length, void *arg)
{
    priskv_backend_request *req = arg;

    req->cb(status, length, req->cbarg);
    free(req);
}

static void priskv_backend_child_set_cb(priskv_backend_status status, uint32_t length, void *arg)
{
    priskv_backend_request *req = arg;
    priskv_backend_device *bdev = req->bdev;
    if (status == PRISKV_BACKEND_STATUS_OK) {
        bdev->bdrv->del(bdev, req->key, priskv_backend_set_cb, req);
    } else {
        req->cb(status, length, req->cbarg);
        free(req);
    }
}

void priskv_backend_set(priskv_backend_device *bdev, const char *key, uint8_t *val, uint64_t valuelen,
                      uint64_t timeout, priskv_backend_driver_cb cb, void *cbarg)
{
    priskv_backend_request *req = calloc(1, sizeof(priskv_backend_request));

    req->bdev = bdev;
    req->type = PRISKV_BACKEND_REQUEST_SET;
    req->key = key;
    req->val = val;
    req->valuelen = valuelen;
    req->cb = cb;
    req->cbarg = cbarg;

    if (bdev->child) {
        priskv_backend_set(bdev->child, key, val, valuelen, timeout, priskv_backend_child_set_cb, req);
    } else {
        bdev->bdrv->set(bdev, key, val, valuelen, timeout, priskv_backend_set_cb, req);
    }
}

static void priskv_backend_del_cb(priskv_backend_status status, uint32_t length, void *arg)
{
    priskv_backend_request *req = arg;

    req->cb(status, length, req->cbarg);
    free(req);
}

static void priskv_backend_child_del_cb(priskv_backend_status status, uint32_t length, void *arg)
{
    priskv_backend_request *req = arg;
    priskv_backend_device *bdev = req->bdev;

    if (status == PRISKV_BACKEND_STATUS_OK) {
        bdev->bdrv->del(bdev, req->key, priskv_backend_del_cb, req);
    } else {
        req->cb(status, length, req->cbarg);
        free(req);
    }
}

void priskv_backend_del(priskv_backend_device *bdev, const char *key, priskv_backend_driver_cb cb,
                      void *cbarg)
{
    priskv_backend_request *req = calloc(1, sizeof(priskv_backend_request));

    req->bdev = bdev;
    req->type = PRISKV_BACKEND_REQUEST_DEL;
    req->key = key;
    req->cb = cb;
    req->cbarg = cbarg;

    if (bdev->child) {
        priskv_backend_del(bdev->child, key, priskv_backend_child_del_cb, req);
    } else {
        bdev->bdrv->del(bdev, key, priskv_backend_del_cb, req);
    }
}

static void priskv_backend_test_cb(priskv_backend_status status, uint32_t length, void *arg)
{
    priskv_backend_request *req = arg;
    priskv_backend_device *bdev = req->bdev;

    if (status == PRISKV_BACKEND_STATUS_OK) {
        req->cb(status, length, req->cbarg);
        free(req);
        return;
    }

    if (bdev->child) {
        priskv_backend_test(bdev->child, req->key, req->cb, req->cbarg);
    } else {
        req->cb(status, length, req->cbarg);
    }
    free(req);
    return;
}

void priskv_backend_test(priskv_backend_device *bdev, const char *key, priskv_backend_driver_cb cb,
                       void *cbarg)
{
    priskv_backend_request *req = calloc(1, sizeof(priskv_backend_request));

    req->bdev = bdev;
    req->type = PRISKV_BACKEND_REQUEST_TEST;
    req->key = key;
    req->cb = cb;
    req->cbarg = cbarg;

    bdev->bdrv->test(bdev, key, priskv_backend_test_cb, req);
}

/* Backend lifecycle management */
char *tiering_backend_address = NULL;
bool tiering_enabled = false;


static void priskv_thread_backend_init_hook(priskv_thread *thd, void *arg)
{
    priskv_backend_device *bdev;
    int epollfd;

    if (!tiering_enabled || !tiering_backend_address) {
        return;
    }

    epollfd = priskv_thread_get_epollfd(thd);
    if (epollfd < 0) {
        priskv_log_error("BACKEND: failed to get epollfd for thread\n");
        return;
    }

    bdev = priskv_backend_open(tiering_backend_address, epollfd);
    if (!bdev) {
        priskv_log_error("BACKEND: failed to open backend device for thread\n");
        return;
    }

    priskv_thread_set_user_data(thd, bdev);
    priskv_log_debug("BACKEND: device opened for thread%u\n", thd);
}

static void priskv_thread_backend_cleanup_hook(priskv_thread *thd, void *arg)
{
    priskv_backend_device *bdev;

    if (!tiering_enabled) {
        return;
    }

    bdev = priskv_thread_get_user_data(thd);
    if (!bdev) {
        return;
    }

    if (priskv_backend_close(bdev)) {
        priskv_log_error("BACKEND: failed to close backend device for thread %u\n", thd);
    } else {
        priskv_log_debug("BACKEND: device closed for thread %u\n", thd);
    }

    priskv_thread_set_user_data(thd, NULL);
}

priskv_backend_device *priskv_get_thread_backend(priskv_thread *thread)
{
    if (!thread || !tiering_enabled) {
        return NULL;
    }

    return (priskv_backend_device *)priskv_thread_get_user_data(thread);
}

priskv_thread_hooks *priskv_get_thread_backend_hooks(void)
{
    static priskv_thread_hooks tiering_hooks = {.init = priskv_thread_backend_init_hook,
                                              .cleanup = priskv_thread_backend_cleanup_hook,
                                              .arg = NULL};

    return tiering_enabled ? &tiering_hooks : NULL;
}
