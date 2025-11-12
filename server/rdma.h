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

#ifndef __PRISKV_SERVER_RDMA__
#define __PRISKV_SERVER_RDMA__

#if defined(__cplusplus)
extern "C"
{
#endif

#include <limits.h>
#include <stdbool.h>

#include "priskv-protocol.h"

#define PRISKV_RDMA_MAX_BIND_ADDR 32
#define PRISKV_RDMA_DEFAULT_PORT ('H' << 8 | 'P')

#define PRISKV_RDMA_MAX_INFLIGHT_COMMAND 4096
#define PRISKV_RDMA_DEFAULT_INFLIGHT_COMMAND 128
#define PRISKV_RDMA_MAX_SGL 8
#define PRISKV_RDMA_DEFAULT_SGL 4
#define PRISKV_RDMA_MAX_KEY (1 << 30)
#define PRISKV_RDMA_DEFAULT_KEY (16 * 1024)
#define PRISKV_RDMA_MAX_KEY_LENGTH 1024
#define PRISKV_RDMA_DEFAULT_KEY_LENGTH 128
#define PRISKV_RDMA_MAX_VALUE_BLOCK_SIZE (1 << 20)
#define PRISKV_RDMA_DEFAULT_VALUE_BLOCK_SIZE 4096
#define PRISKV_RDMA_MAX_VALUE_BLOCK (1UL << 30)
#define PRISKV_RDMA_DEFAULT_VALUE_BLOCK (1024UL * 1024)
#define SLOW_QUERY_THRESHOLD_LATENCY_US 1000000 /* 1 second */

extern uint32_t g_slow_query_threshold_latency_us;

typedef struct priskv_rdma_stats {
    uint64_t ops;
    uint64_t bytes;
} priskv_rdma_stats;

typedef struct priskv_rdma_conn_cap {
    uint16_t max_sgl;
    uint16_t max_key_length;
    uint16_t max_inflight_command;
} priskv_rdma_conn_cap;

typedef struct priskv_rdma_client {
    char address[PRISKV_ADDR_LEN];
    priskv_rdma_stats stats[PRISKV_COMMAND_MAX];
    uint64_t resps;
    bool closing;
} priskv_rdma_client;

typedef struct priskv_rdma_listener {
    char address[PRISKV_ADDR_LEN];
    int nclients;
    priskv_rdma_client *clients;
} priskv_rdma_listener;

int priskv_rdma_listen(char **addr, int naddr, int port, void *kv, priskv_rdma_conn_cap *cap);
int priskv_rdma_get_fd(void);
void priskv_rdma_process(void);

void *priskv_rdma_get_kv(void);

priskv_rdma_listener *priskv_rdma_get_listeners(int *nlisteners);
void priskv_rdma_free_listeners(priskv_rdma_listener *listeners, int nlisteners);

#if defined(__cplusplus)
}
#endif

#endif /* __PRISKV_SERVER_RDMA__ */
