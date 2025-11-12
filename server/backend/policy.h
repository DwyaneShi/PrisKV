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

#ifndef __PRISKV_POLICY_H__
#define __PRISKV_POLICY_H__

#include <stdbool.h>
#include "list.h"

typedef struct priskv_policy priskv_policy;
typedef struct priskv_policy_impl priskv_policy_impl;

struct priskv_policy {
    priskv_policy_impl *impl;
    void *opaque;
};

struct priskv_policy_impl {
    const char *name;
    void *(*create)(void);
    void (*access)(void *opaque, const char *key);
    const char *(*evict)(void *opaque);
    void (*del_key)(void *opaque, const char *key);
    void (*destroy)(void *opaque);
    bool (*try_ref_key)(void *opaque, const char *key);
    void (*unref_key)(void *opaque, const char *key);

    struct list_node node;
};

void priskv_policy_register(priskv_policy_impl *policy);

#define policy_init(function)                                                                      \
    static void __attribute__((constructor)) priskv_init_##function(void)                          \
    {                                                                                              \
        function();                                                                                \
    }

// Not thread-safe, caller must handle concurrency
priskv_policy *priskv_policy_create(const char *name);
void priskv_policy_destroy(priskv_policy *policy);
void priskv_policy_access(priskv_policy *policy, const char *key);
const char *priskv_policy_evict(priskv_policy *policy);
void priskv_policy_del_key(priskv_policy *policy, const char *key);
bool priskv_policy_try_ref_key(priskv_policy *policy, const char *key);
void priskv_policy_unref_key(priskv_policy *policy, const char *key);

#endif /* __PRISKV_POLICY_H__ */
