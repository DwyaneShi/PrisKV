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

#include "policy.h"
#include "list.h"

static struct list_head global_policy_list = LIST_HEAD_INIT(global_policy_list);

void priskv_policy_register(priskv_policy_impl *policy)
{
    priskv_policy_impl *tmp;

    assert(policy->name);
    list_for_each (&global_policy_list, tmp, node) {
        assert(strcmp(tmp->name, policy->name));
    }

    list_add_tail(&global_policy_list, &policy->node);
}

static priskv_policy_impl *priskv_policy_find(const char *name)
{
    priskv_policy_impl *tmp;

    list_for_each (&global_policy_list, tmp, node) {
        if (strcmp(tmp->name, name) == 0) {
            return tmp;
        }
    }

    return NULL;
}

priskv_policy *priskv_policy_create(const char *name)
{
    priskv_policy_impl *impl = priskv_policy_find(name);
    if (impl == NULL) {
        return NULL;
    }

    priskv_policy *policy = malloc(sizeof(priskv_policy));
    if (policy == NULL) {
        return NULL;
    }

    policy->impl = impl;
    policy->opaque = impl->create();
    if (policy->opaque == NULL) {
        free(policy);
        return NULL;
    }

    return policy;
}

void priskv_policy_destroy(priskv_policy *policy)
{
    if (policy == NULL) {
        return;
    }

    policy->impl->destroy(policy->opaque);
    free(policy);
}

void priskv_policy_access(priskv_policy *policy, const char *key)
{
    if (policy == NULL) {
        return;
    }

    policy->impl->access(policy->opaque, key);
}

const char *priskv_policy_evict(priskv_policy *policy)
{
    if (policy == NULL) {
        return NULL;
    }

    return policy->impl->evict(policy->opaque);
}

void priskv_policy_del_key(priskv_policy *policy, const char *key)
{
    if (policy == NULL) {
        return;
    }

    policy->impl->del_key(policy->opaque, key);
}

bool priskv_policy_try_ref_key(priskv_policy *policy, const char *key)
{
    if (policy == NULL || policy->impl->try_ref_key == NULL) {
        return false;
    }

    return policy->impl->try_ref_key(policy->opaque, key);
}

void priskv_policy_unref_key(priskv_policy *policy, const char *key)
{
    if (policy == NULL || policy->impl->unref_key == NULL) {
        return;
    }

    policy->impl->unref_key(policy->opaque, key);
}
