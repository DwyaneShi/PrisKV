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
#include "buddy.h"

static void test_buddy_small()
{
    uint32_t nmemb = 32;
    uint32_t size = 128;
    void *buddy;
    uint8_t *base;

    base = malloc(priskv_buddy_mem_size(nmemb, size));
    assert(base);

    buddy = priskv_buddy_create(base, nmemb, size);
    assert(buddy);
    assert(priskv_buddy_base(buddy) == base);
    assert(priskv_buddy_size(buddy) == size);
    assert(priskv_buddy_nmemb(buddy) == nmemb);

    assert(nmemb == priskv_buddy_nmemb(buddy));
    assert(size == priskv_buddy_size(buddy));
    base = priskv_buddy_base(buddy);
    assert(base);

    /* round 1, allocate elem with @size, it should be offset 0
     * +---------------------------------------+
     * |1000 0000 0000 0000 0000 0000 0000 0000|
     * +---------------------------------------+
     */
    uint8_t *elem0 = priskv_buddy_alloc(buddy, size);
    assert(elem0 == base);
    assert(priskv_buddy_inuse(buddy) == 1);

    /* round 2, allocate elem with @size, it should be offset @size * 2
     * +---------------------------------------+
     * |1011 0000 0000 0000 0000 0000 0000 0000|
     * +---------------------------------------+
     */
    uint8_t *elem2 = priskv_buddy_alloc(buddy, size * 2);
    assert(elem2 == (base + size * 2));
    assert(priskv_buddy_inuse(buddy) == 3);

    /* round 3, allocate elem with @size * 3, it should be offset @size * 4
     * +---------------------------------------+
     * |1011 1111 0000 0000 0000 0000 0000 0000|
     * +---------------------------------------+
     */
    uint8_t *elem4 = priskv_buddy_alloc(buddy, size * 3);
    assert(elem4 == (base + size * 4));
    assert(priskv_buddy_inuse(buddy) == 7);

    /* round 4, allocate elem with @size, it should be offset @size * 1
     * +---------------------------------------+
     * |1111 1111 0000 0000 0000 0000 0000 0000|
     * +---------------------------------------+
     */
    uint8_t *elem1 = priskv_buddy_alloc(buddy, size);
    assert(elem1 == (base + size * 1));
    assert(priskv_buddy_inuse(buddy) == 8);

    /* round 5, free elem 2, and re-alloc @size * 2 */
    priskv_buddy_free(buddy, elem2);
    assert(priskv_buddy_inuse(buddy) == 6);
    uint8_t *elem = priskv_buddy_alloc(buddy, size * 2);
    assert(elem == elem2);
    assert(priskv_buddy_inuse(buddy) == 8);

    /* round 6, allocate elem with @size * 4, it should be offset @size * 8
     * +---------------------------------------+
     * |1111 1111 1111 0000 0000 0000 0000 0000|
     * +---------------------------------------+
     */
    uint8_t *elem8 = priskv_buddy_alloc(buddy, size * 4);
    assert(elem8 == (base + size * 8));
    assert(priskv_buddy_inuse(buddy) == 12);

    /* round 7, allocate elem with @size * 8, it should be offset @size * 16
     * +---------------------------------------+
     * |1111 1111 1111 0000 1111 1111 0000 0000|
     * +---------------------------------------+
     */
    uint8_t *elem16 = priskv_buddy_alloc(buddy, size * 8);
    assert(elem16 == (base + size * 16));
    assert(priskv_buddy_inuse(buddy) == 20);

    /* round 8, allocate elem with @size * 6, it should be offset @size * 24
     * +---------------------------------------+
     * |1111 1111 1111 0000 1111 1111 1111 1111|
     * +---------------------------------------+
     */
    uint8_t *elem24 = priskv_buddy_alloc(buddy, size * 6);
    assert(elem24 == (base + size * 24));
    assert(priskv_buddy_inuse(buddy) == 28);

    /* round 9, allocate elem with @size * 6, it should be NULL
     * +---------------------------------------+
     * |1111 1111 1111 0000 1111 1111 1111 1111|
     * +---------------------------------------+
     */
    uint8_t *elemnull = priskv_buddy_alloc(buddy, size * 6);
    assert(elemnull == NULL);
    assert(priskv_buddy_inuse(buddy) == 28);

    /* round 10, allocate elem with @size * 15, it should be NULL
     * +---------------------------------------+
     * |1111 1111 1111 0000 1111 1111 1111 1111|
     * +---------------------------------------+
     */
    elemnull = priskv_buddy_alloc(buddy, size * 15);
    assert(elemnull == NULL);
    assert(priskv_buddy_inuse(buddy) == 28);

    /* round 11, allocate elem with @size * 3, it should be offset @size * 12
     * +---------------------------------------+
     * |1111 1111 1111 1111 1111 1111 1111 1111|
     * +---------------------------------------+
     */
    uint8_t *elem12 = priskv_buddy_alloc(buddy, size * 3);
    assert(elem12 == (base + size * 12));
    assert(priskv_buddy_inuse(buddy) == 32);

    /* round 12, allocate elem with @size, it should be NULL
     * +---------------------------------------+
     * |1111 1111 1111 1111 1111 1111 1111 1111|
     * +---------------------------------------+
     */
    elemnull = priskv_buddy_alloc(buddy, size);
    assert(elemnull == NULL);
    assert(priskv_buddy_inuse(buddy) == 32);

    /* round 13, free elem8 [8~12), and re-alloc @size * 2
     * +---------------------------------------+
     * |1111 1111 1100 1111 1111 1111 1111 1111|
     * +---------------------------------------+
     */
    priskv_buddy_free(buddy, elem8);
    elem8 = priskv_buddy_alloc(buddy, size * 2);
    assert(elem8 == base + size * 8);
    assert(priskv_buddy_inuse(buddy) == 30);

    /* round 14, allocate elem with @size, it should be offset @size * 10
     * +---------------------------------------+
     * |1111 1111 1110 1111 1111 1111 1111 1111|
     * +---------------------------------------+
     */
    uint8_t *elem10 = priskv_buddy_alloc(buddy, size);
    assert(elem10 == base + size * 10);
    assert(priskv_buddy_inuse(buddy) == 31);

    /* round 15, allocate elem with @size * 2, it should be NULL
     * +---------------------------------------+
     * |1111 1111 1110 1111 1111 1111 1111 1111|
     * +---------------------------------------+
     */
    elemnull = priskv_buddy_alloc(buddy, size * 2);
    assert(elemnull == NULL);
    assert(priskv_buddy_inuse(buddy) == 31);

    /* round 16, allocate elem with @size, it should be offset @size * 11
     * +---------------------------------------+
     * |1111 1111 1111 1111 1111 1111 1111 1111|
     * +---------------------------------------+
     */
    uint8_t *elem11 = priskv_buddy_alloc(buddy, size);
    assert(elem11 == base + size * 11);
    assert(priskv_buddy_inuse(buddy) == 32);

    /* round 17, allocate elem with @size, it should be NULL
     * +---------------------------------------+
     * |1111 1111 1111 1111 1111 1111 1111 1111|
     * +---------------------------------------+
     */
    elemnull = priskv_buddy_alloc(buddy, size);
    assert(elemnull == NULL);
    assert(priskv_buddy_inuse(buddy) == 32);

    /* round 18, free elem8
     * +---------------------------------------+
     * |1111 1111 0011 1111 1111 1111 1111 1111|
     * +---------------------------------------+
     */
    priskv_buddy_free(buddy, elem8);
    assert(priskv_buddy_inuse(buddy) == 30);

    /* round 19, free elem11
     * +---------------------------------------+
     * |1111 1111 0010 1111 1111 1111 1111 1111|
     * +---------------------------------------+
     */
    priskv_buddy_free(buddy, elem11);
    assert(priskv_buddy_inuse(buddy) == 29);

    /* round 20, free elem24
     * +---------------------------------------+
     * |1111 1111 0010 1111 1111 1111 0000 0000|
     * +---------------------------------------+
     */
    priskv_buddy_free(buddy, elem24);
    assert(priskv_buddy_inuse(buddy) == 21);

    /* round 21, free elem16
     * +---------------------------------------+
     * |1111 1111 0010 1111 0000 0000 0000 0000|
     * +---------------------------------------+
     */
    priskv_buddy_free(buddy, elem16);
    assert(priskv_buddy_inuse(buddy) == 13);

    /* round 22, free elem0
     * +---------------------------------------+
     * |0111 1111 0010 1111 0000 0000 0000 0000|
     * +---------------------------------------+
     */
    priskv_buddy_free(buddy, elem0);
    assert(priskv_buddy_inuse(buddy) == 12);

    /* round 23, free elem2
     * +---------------------------------------+
     * |0100 1111 0010 1111 0000 0000 0000 0000|
     * +---------------------------------------+
     */
    priskv_buddy_free(buddy, elem2);
    assert(priskv_buddy_inuse(buddy) == 10);

    /* round 24, free elem1
     * +---------------------------------------+
     * |0000 1111 0010 1111 0000 0000 0000 0000|
     * +---------------------------------------+
     */
    priskv_buddy_free(buddy, elem1);
    assert(priskv_buddy_inuse(buddy) == 9);

    /* round 25, free elem4
     * +---------------------------------------+
     * |0000 0000 0010 1111 0000 0000 0000 0000|
     * +---------------------------------------+
     */
    priskv_buddy_free(buddy, elem4);
    assert(priskv_buddy_inuse(buddy) == 5);

    /* round 26, free elem10
     * +---------------------------------------+
     * |0000 0000 0000 1111 0000 0000 0000 0000|
     * +---------------------------------------+
     */
    priskv_buddy_free(buddy, elem10);
    assert(priskv_buddy_inuse(buddy) == 4);

    /* round 27, free elem12
     * +---------------------------------------+
     * |0000 0000 0000 0000 0000 0000 0000 0000|
     * +---------------------------------------+
     */
    priskv_buddy_free(buddy, elem12);
    assert(priskv_buddy_inuse(buddy) == 0);

    priskv_buddy_destroy(buddy);
    free(base);
}

static void test_buddy_4GB()
{
    uint32_t nmemb = 1;
    uint32_t size = UINT32_MAX;
    void *buddy;
    uint8_t *base;

    base = malloc(priskv_buddy_mem_size(nmemb, size));
    assert(base);

    buddy = priskv_buddy_create(base, nmemb, size);
    assert(buddy);
    assert(priskv_buddy_base(buddy) == base);
    assert(priskv_buddy_size(buddy) == size);
    assert(priskv_buddy_nmemb(buddy) == nmemb);
    assert(priskv_buddy_base(buddy) == base);

    uint8_t *elem_4GB = priskv_buddy_alloc(buddy, size);
    assert(elem_4GB == base);

    priskv_buddy_free(buddy, elem_4GB);
    assert(priskv_buddy_inuse(buddy) == 0);
    priskv_buddy_destroy(buddy);
    free(base);
}

int main()
{
    test_buddy_small();
    test_buddy_4GB();

    return 0;
}
