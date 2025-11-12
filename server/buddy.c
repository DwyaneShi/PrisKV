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

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "buddy.h"

#include "priskv-utils.h"

struct buddy {
    uint32_t nmemb;
    uint32_t inuse;
    uint32_t size;
    uint8_t *base;
    uint32_t *meta;
    pthread_mutex_t lock;
};

#define L_LEAF(index) ((index) * 2 + 1)
#define R_LEAF(index) ((index) * 2 + 2)
#define PARENT(index) (((index) + 1) / 2 - 1)

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static inline uint32_t roundup_power_of_2(uint32_t val)
{
    return 1 << (sizeof(uint32_t) * 8 - __builtin_clz(val));
}

void *priskv_buddy_create(void *base, uint32_t nmemb, uint32_t size)
{
    struct buddy *buddy;
    uint32_t nodes, i;

    if (!base) {
        return NULL;
    }

    if (!IS_POWER_OF_2(nmemb)) {
        return NULL;
    }

    nodes = nmemb * 2;
    buddy = (struct buddy *)calloc(1, sizeof(struct buddy));
    if (!buddy) {
        return NULL;
    }

    buddy->nmemb = nmemb;
    buddy->size = size;
    buddy->base = base;
    buddy->meta = (uint32_t *)(buddy->base + (uint64_t)nmemb * (uint64_t)size);
    pthread_mutex_init(&buddy->lock, 0);

    for (i = 0; i < buddy->nmemb * 2 - 1; i++) {
        if (IS_POWER_OF_2(i + 1)) {
            nodes /= 2;
        }

        buddy->meta[i] = nodes;
    }

    return buddy;
}

void priskv_buddy_destroy(void *buddy)
{
    struct buddy *__buddy = (struct buddy *)buddy;
    pthread_mutex_destroy(&__buddy->lock);

    free(__buddy);
}

void *priskv_buddy_base(void *buddy)
{
    struct buddy *__buddy = (struct buddy *)buddy;

    return __buddy->base;
}

uint32_t priskv_buddy_size(void *buddy)
{
    struct buddy *__buddy = (struct buddy *)buddy;

    return __buddy->size;
}

uint32_t priskv_buddy_nmemb(void *buddy)
{
    struct buddy *__buddy = (struct buddy *)buddy;

    return __buddy->nmemb;
}

uint32_t priskv_buddy_inuse(void *buddy)
{
    struct buddy *__buddy = (struct buddy *)buddy;

    return __buddy->inuse;
}

void *priskv_buddy_alloc(void *buddy, uint32_t size)
{
    struct buddy *__buddy = (struct buddy *)buddy;
    uint32_t index = 0;
    uint32_t nodes;
    uint64_t offset = 0;
    uint32_t alignup = ((uint64_t)size + __buddy->size - 1) / __buddy->size;
    void *addr = NULL;

    pthread_mutex_lock(&__buddy->lock);
    if (!IS_POWER_OF_2(alignup)) {
        alignup = roundup_power_of_2(alignup);
    }

    if (__buddy->meta[index] < alignup) {
        goto end;
    }

    for (nodes = __buddy->nmemb; nodes != alignup; nodes /= 2) {
        if (__buddy->meta[L_LEAF(index)] >= alignup) {
            index = L_LEAF(index);
        } else {
            index = R_LEAF(index);
        }
    }

    if (!__buddy->meta[index]) {
        goto end;
    }

    __buddy->meta[index] = 0;
    offset = (index + 1) * nodes - __buddy->nmemb;

    while (index) {
        index = PARENT(index);
        __buddy->meta[index] = MAX(__buddy->meta[L_LEAF(index)], __buddy->meta[R_LEAF(index)]);
    }
    addr = __buddy->base + offset * __buddy->size;
    __buddy->inuse += alignup;

end:
    pthread_mutex_unlock(&__buddy->lock);
    return addr;
}

void priskv_buddy_free(void *buddy, void *addr)
{
    struct buddy *__buddy = (struct buddy *)buddy;
    uint32_t nodes, index = 0;
    uint32_t left_meta, right_meta;
    uint64_t offset;

    pthread_mutex_lock(&__buddy->lock);
    offset = ((uint8_t *)addr - __buddy->base) / __buddy->size;
    if (offset * __buddy->size + __buddy->base != addr) {
        assert(0);
    }

    index = offset + __buddy->nmemb - 1;
    assert(index < 2 * __buddy->nmemb);

    for (nodes = 1; __buddy->meta[index]; index = PARENT(index)) {
        nodes *= 2;
        if (index == 0) {
            goto end;
        }
    }

    assert(index < 2 * __buddy->nmemb);
    __buddy->meta[index] = nodes;
    __buddy->inuse -= nodes;

    while (index) {
        index = PARENT(index);
        nodes *= 2;

        left_meta = __buddy->meta[L_LEAF(index)];
        right_meta = __buddy->meta[R_LEAF(index)];

        if (left_meta + right_meta == nodes) {
            __buddy->meta[index] = nodes;
        } else {
            __buddy->meta[index] = MAX(left_meta, right_meta);
        }
    }

end:
    pthread_mutex_unlock(&__buddy->lock);
}
