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

#ifndef __PRISKV_KVMANAGE_H__
#define __PRISKV_KVMANAGE_H__

#include <event.h>

#include "priskv-protocol.h"

typedef int (*priskv_kvmanage_action)(const char *addr, int port, char **keys, int nkeys,
                                    struct event_base *evbase,
                                    void (*cb)(void *context, int status), void *context);

int priskv_kvmanage_copy_to(const char *addr, int port, char **keys, int nkeys,
                          struct event_base *evbase, void (*cb)(void *context, int status),
                          void *context);

int priskv_kvmanage_move_to(const char *addr, int port, char **keys, int nkeys,
                          struct event_base *evbase, void (*cb)(void *context, int status),
                          void *context);

#endif /* __PRISKV_KVMANAGE_H__ */
