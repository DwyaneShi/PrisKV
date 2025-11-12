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

#ifndef __PRISKV_SERVER_BUDDY__
#define __PRISKV_SERVER_BUDDY__

#if defined(__cplusplus)
extern "C"
{
#endif

#include <stdint.h>

/* the size should be @nmemb * @size + @nmemb * sizeof(uint32_t) + sizeof(struct buddy) */
static inline uint64_t priskv_buddy_mem_size(uint32_t nmemb, uint32_t size)
{
    return (uint64_t)nmemb * ((uint64_t)size + sizeof(uint32_t) * 2);
}

/* create a buddy
 * @base: base address of a chunk memory
 * @nmemb: the count of elements. must be power of 2.
 * @size: bytes of an elements
 */
void *priskv_buddy_create(void *base, uint32_t nmemb, uint32_t size);

void priskv_buddy_destroy(void *buddy);

void *priskv_buddy_alloc(void *buddy, uint32_t size);

void *priskv_buddy_base(void *buddy);

unsigned int priskv_buddy_size(void *buddy);

unsigned int priskv_buddy_nmemb(void *buddy);

unsigned int priskv_buddy_inuse(void *buddy);

void priskv_buddy_free(void *buddy, void *addr);

#if defined(__cplusplus)
}
#endif

#endif /* __PRISKV_SERVER_BUDDY__ */
