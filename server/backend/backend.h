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

#ifndef __PRISKV_BACKEND_H__
#define __PRISKV_BACKEND_H__

#include <stdint.h>
#include <stdbool.h>

#include "list.h"
#include "priskv-threads.h"

typedef struct priskv_backend_driver priskv_backend_driver;
typedef struct priskv_backend_device priskv_backend_device;
typedef struct priskv_backend_link priskv_backend_link;

void priskv_backend_register(priskv_backend_driver *bdrv);

#define backend_init(function)                                                                     \
    static void __attribute__((constructor)) priskv_init_##function(void)                          \
    {                                                                                              \
        function();                                                                                \
    }

typedef enum priskv_backend_status {
    PRISKV_BACKEND_STATUS_OK = 0,
    PRISKV_BACKEND_STATUS_ERROR,
    PRISKV_BACKEND_STATUS_NOT_FOUND,
    PRISKV_BACKEND_STATUS_VALUE_TOO_BIG,
    PRISKV_BACKEND_STATUS_TIMEOUT,
    PRISKV_BACKEND_STATUS_NO_SPACE,
} priskv_backend_status;

// TEST and GET need to return the actual value length to the upper layer,
// SET and DEL operations do not need to return length, only the status is required.
typedef void (*priskv_backend_driver_cb)(priskv_backend_status status, uint32_t length, void *arg);

struct priskv_backend_driver {
    struct list_node node;
    const char *name;
    int (*open)(priskv_backend_device *bdev);
    int (*close)(priskv_backend_device *bdev);
    bool (*is_cacheable)(priskv_backend_device *bdev, uint64_t valuelen);
    void (*get)(priskv_backend_device *bdev, const char *key, uint8_t *val, uint64_t valuelen,
                priskv_backend_driver_cb cb, void *cbarg);
    void (*set)(priskv_backend_device *bdev, const char *key, uint8_t *val, uint64_t valuelen,
                uint64_t timeout, priskv_backend_driver_cb cb, void *cbarg);
    void (*del)(priskv_backend_device *bdev, const char *key, priskv_backend_driver_cb cb,
                void *cbarg);
    void (*test)(priskv_backend_device *bdev, const char *key, priskv_backend_driver_cb cb,
                 void *cbarg);
    void (*evict)(priskv_backend_device *bdev, priskv_backend_driver_cb cb, void *cbarg);
    int (*clearup)(priskv_backend_device *bdev);
};

struct priskv_backend_link {
    char *protocol;
    char *address;
    char *childaddr;
};

/*
 * priskv_backend_device is thread-level, that means each thread has its own priskv_backend_device
 * context.
 */
struct priskv_backend_device {
    priskv_backend_link link;
    priskv_backend_driver *bdrv;
    void *private_data;
    priskv_backend_device *child;
    int epollfd;
};

/*
 * backend address format: protocol1:address1;protocol2:address2;protocol3:address3;
 * can we specify cache policy or other device config in address?
 * for example, localfs:/data/priskv&size=100GB&evict=lru
 */
priskv_backend_device *priskv_backend_open(const char *address, int epollfd);
int priskv_backend_close(priskv_backend_device *bdev);

void priskv_backend_get(priskv_backend_device *bdev, const char *key, uint8_t *val,
                        uint64_t valuelen, priskv_backend_driver_cb cb, void *cbarg);
void priskv_backend_set(priskv_backend_device *bdev, const char *key, uint8_t *val,
                        uint64_t valuelen, uint64_t timeout, priskv_backend_driver_cb cb,
                        void *cbarg);
void priskv_backend_del(priskv_backend_device *bdev, const char *key, priskv_backend_driver_cb cb,
                        void *cbarg);
void priskv_backend_test(priskv_backend_device *bdev, const char *key, priskv_backend_driver_cb cb,
                         void *cbarg);

priskv_backend_device *priskv_get_thread_backend(priskv_thread *thread);
struct priskv_thread_hooks *priskv_get_thread_backend_hooks(void);

extern bool tiering_enabled;

static inline bool priskv_backend_tiering_enabled(void)
{
    return tiering_enabled;
}

#endif /* __PRISKV_BACKEND_H__ */
