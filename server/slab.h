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

#ifndef __PRISKV_SERVER_SLAB__
#define __PRISKV_SERVER_SLAB__

#if defined(__cplusplus)
extern "C"
{
#endif

#include <stdint.h>

/* create a slab
 * @name: name of slab, as max as 64 bytes.
 * @base: base address of a chunk memory, the size should be @objects * @size.
 * @objects: the count of elements.
 * @size: bytes of an elements.
 */
void *priskv_slab_create(const char *name, void *base, uint32_t size, uint32_t objects);
void priskv_slab_destroy(void *slab);
void *priskv_slab_reserve(void *slab, int index);
void *priskv_slab_alloc(void *slab);
void priskv_slab_free(void *slab, void *addr);
int priskv_slab_index(void *slab, void *addr);
const char *priskv_slab_name(void *slab);
void *priskv_slab_base(void *slab);
uint32_t priskv_slab_size(void *slab);
uint32_t priskv_slab_objects(void *slab);
uint32_t priskv_slab_inuse(void *slab);
void *priskv_slab_bitmap(void *slab);

#if defined(__cplusplus)
}
#endif

#endif /* __PRISKV_SERVER_SLAB__ */
