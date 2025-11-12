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

#include "list.h"
#include "uthash.h"
#include "policy.h"

typedef struct priskv_lru_node {
    struct list_node node;
    const char *key;
    int ref_count;
    UT_hash_handle hh;
} priskv_lru_node;

typedef struct priskv_policy_lru {
    struct list_head head;
    priskv_lru_node *hashmap;
} priskv_policy_lru;

static void *lru_policy_create()
{
    priskv_policy_lru *lru = malloc(sizeof(priskv_policy_lru));
    if (!lru) {
        return NULL;
    }

    list_head_init(&lru->head);
    lru->hashmap = NULL;

    return lru;
}

static void lru_policy_access(void *opaque, const char *key)
{
    priskv_policy_lru *lru = opaque;
    priskv_lru_node *node;

    HASH_FIND_STR(lru->hashmap, key, node);
    if (node) {
        list_del(&node->node);
    } else {
        node = malloc(sizeof(priskv_lru_node));
        node->key = strdup(key);
        node->ref_count = 0;
        HASH_ADD_STR(lru->hashmap, key, node);
    }

    list_add(&lru->head, &node->node);
}

static const char *lru_policy_evict(void *opaque)
{
    priskv_policy_lru *lru = opaque;
    priskv_lru_node *node;
    const char *evicted_key = NULL;
    int max_attempts = 128;

    node = list_tail(&lru->head, priskv_lru_node, node);
    while (node && max_attempts-- > 0) {
        if (node->ref_count == 0) {
            evicted_key = node->key;
            list_del(&node->node);
            HASH_DEL(lru->hashmap, node);
            free(node);
            return evicted_key;
        }
        // Move to previous node (towards head)
        node = list_prev(&lru->head, node, node);
    }

    return NULL;
}

static void lru_policy_del_key(void *opaque, const char *key)
{
    priskv_policy_lru *lru = opaque;
    priskv_lru_node *node;

    HASH_FIND_STR(lru->hashmap, key, node);
    if (node) {
        list_del(&node->node);
        HASH_DEL(lru->hashmap, node);
        free((char *)node->key);
        free(node);
    }
}

static bool lru_policy_try_ref_key(void *opaque, const char *key)
{
    priskv_policy_lru *lru = opaque;
    priskv_lru_node *node;

    HASH_FIND_STR(lru->hashmap, key, node);
    if (node) {
        node->ref_count++;
        // Move to head of LRU list
        list_del(&node->node);
        list_add(&lru->head, &node->node);
        return true;
    }
    return false;
}

static void lru_policy_unref_key(void *opaque, const char *key)
{
    priskv_policy_lru *lru = opaque;
    priskv_lru_node *node;

    HASH_FIND_STR(lru->hashmap, key, node);
    if (node && node->ref_count > 0) {
        node->ref_count--;
    }
}

static void lru_policy_destroy(void *opaque)
{
    priskv_policy_lru *lru = opaque;
    priskv_lru_node *node, *tmp;

    HASH_ITER(hh, lru->hashmap, node, tmp)
    {
        HASH_DEL(lru->hashmap, node);
        free((char *)node->key);
        free(node);
    }

    free(lru);
}

static priskv_policy_impl lru_policy = {
    .name = "lru",
    .create = lru_policy_create,
    .access = lru_policy_access,
    .evict = lru_policy_evict,
    .del_key = lru_policy_del_key,
    .destroy = lru_policy_destroy,
    .try_ref_key = lru_policy_try_ref_key,
    .unref_key = lru_policy_unref_key,
};

static void priskv_policy_init_lru()
{
    priskv_policy_register(&lru_policy);
}

policy_init(priskv_policy_init_lru);
