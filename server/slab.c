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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "slab.h"
#include "priskv-utils.h"

typedef struct priskv_slab {
#define PRISKV_SLAB_NAME_LEN 64
    char name[PRISKV_SLAB_NAME_LEN];
    uint32_t size;
    uint32_t inuse;
    uint32_t objects;
    uint8_t *base;
    uint64_t lindex;
    pthread_spinlock_t spin;
    uint64_t bitmap[0];
} priskv_slab;

#define BITS_OF_UL (sizeof(uint64_t) * 8)

static inline void set_bit(uint64_t *val, uint32_t bit)
{
    *val |= (1UL << bit);
}

static inline void clear_bit(uint64_t *val, uint32_t bit)
{
    *val &= ~(1UL << bit);
}

void *priskv_slab_create(const char *name, void *base, uint32_t size, uint32_t objects)
{
    priskv_slab *__slab;
    uint64_t *ptr;
    uint32_t bits, index;

    if (!base) {
        return NULL;
    }

    if (!size) {
        return NULL;
    }

    if (!objects) {
        return NULL;
    }

    bits = ALIGN_UP(objects, BITS_OF_UL);
    __slab = (priskv_slab *)calloc(1, sizeof(*__slab) + bits / 8);
    if (!__slab) {
        return NULL;
    }

    /* mark all available bits */
    for (index = 0; index < objects; index++) {
        ptr = &__slab->bitmap[(index / BITS_OF_UL)];
        set_bit(ptr, index % BITS_OF_UL);
    }

    strncpy(__slab->name, name, sizeof(__slab->name) - 1);
    __slab->size = size;
    __slab->objects = objects;
    __slab->base = base;
    __slab->lindex = 0;
    pthread_spin_init(&__slab->spin, 0);

    return __slab;
}

void priskv_slab_destroy(void *slab)
{
    priskv_slab *__slab = (priskv_slab *)slab;
    pthread_spin_destroy(&__slab->spin);

    free(__slab);
}

static void *__slab_alloc(priskv_slab *__slab, uint64_t *ptr, uint64_t index)
{
    uint32_t found = 0;

    found = __builtin_ffsl(*ptr);
    found--;
    clear_bit(ptr, found);
    assert(index * BITS_OF_UL + found < __slab->objects);
    __slab->inuse++;

    return __slab->base + (index * BITS_OF_UL + found) * __slab->size;
}

void *priskv_slab_reserve(void *slab, int index)
{
    priskv_slab *__slab = (priskv_slab *)slab;
    uint64_t *ptr;

    assert(index >= 0);
    assert(index < __slab->objects);
    ptr = &__slab->bitmap[index / BITS_OF_UL];
    clear_bit(ptr, index % BITS_OF_UL);
    __slab->inuse++;

    return __slab->base + (uint64_t)index * __slab->size;
}

void *priskv_slab_alloc(void *slab)
{
    priskv_slab *__slab = (priskv_slab *)slab;
    uint64_t *ptr, index;
    uint32_t bits;
    uint8_t *addr = NULL;

    pthread_spin_lock(&__slab->spin);
    bits = ALIGN_UP(__slab->objects, BITS_OF_UL);
    for (index = __slab->lindex; index < bits / BITS_OF_UL; index++) {
        ptr = &__slab->bitmap[index];
        if (!*ptr) {
            continue;
        }
        addr = __slab_alloc(__slab, ptr, index);
        break;
    }

    if (addr == NULL) {
        for (index = 0; index < __slab->lindex; index++) {
            ptr = &__slab->bitmap[index];
            if (!*ptr) {
                continue;
            }
            addr = __slab_alloc(__slab, ptr, index);
            break;
        }
    }
    if (addr != NULL) {
        __slab->lindex = index;
    }

    pthread_spin_unlock(&__slab->spin);
    return addr;
}

static inline int __priskv_slab_index(void *slab, void *addr)
{
    priskv_slab *__slab = (priskv_slab *)slab;
    uint8_t *__addr = (uint8_t *)addr;
    int64_t index;

    if ((__addr - __slab->base) % __slab->size) {
        return -1;
    }

    index = (__addr - __slab->base) / __slab->size;
    assert(index < __slab->objects);

    return index;
}

void priskv_slab_free(void *slab, void *addr)
{
    priskv_slab *__slab = (priskv_slab *)slab;
    uint64_t *ptr;
    int index;

    pthread_spin_lock(&__slab->spin);
    index = __priskv_slab_index(slab, addr);
    assert(index >= 0);
    ptr = &__slab->bitmap[index / BITS_OF_UL];
    set_bit(ptr, index % BITS_OF_UL);
    __slab->inuse--;
    assert(__slab->inuse < __slab->objects);
    pthread_spin_unlock(&__slab->spin);
}

int priskv_slab_index(void *slab, void *addr)
{
    return __priskv_slab_index(slab, addr);
}

const char *priskv_slab_name(void *slab)
{
    priskv_slab *__slab = (priskv_slab *)slab;

    return __slab->name;
}

void *priskv_slab_base(void *slab)
{
    priskv_slab *__slab = (priskv_slab *)slab;

    return __slab->base;
}

uint32_t priskv_slab_size(void *slab)
{
    priskv_slab *__slab = (priskv_slab *)slab;

    return __slab->size;
}

uint32_t priskv_slab_objects(void *slab)
{
    priskv_slab *__slab = (priskv_slab *)slab;

    return __slab->objects;
}

uint32_t priskv_slab_inuse(void *slab)
{
    priskv_slab *__slab = (priskv_slab *)slab;

    return __slab->inuse;
}

void *priskv_slab_bitmap(void *slab)
{
    priskv_slab *__slab = (priskv_slab *)slab;

    return __slab->bitmap;
}
