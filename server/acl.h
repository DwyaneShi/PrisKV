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

#ifndef __PRISKV_SERVER_ACL__
#define __PRISKV_SERVER_ACL__

#if defined(__cplusplus)
extern "C"
{
#endif

#include <stdint.h>
#include <netinet/in.h>

#include "list.h"

typedef struct priskv_acl {
    struct list_node entry;
    char *rule;
    union {
        struct sockaddr_in addr4;
        struct sockaddr_in6 addr6;
        struct sockaddr addr;
    };
    int mask_bits;
} priskv_acl;

int priskv_acl_add(const char *rule);
int priskv_acl_del(const char *rule);
int priskv_acl_verify(const struct sockaddr *saddr);
int __priskv_acl_verify(const char *addr);
char **priskv_acl_get_rules(int *nrules);
void priskv_acl_free_rules(char **rules, int nrules);

#if defined(__cplusplus)
}
#endif

#endif /* __PRISKV_SERVER_ACL__ */
