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

#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "client.h"
#include "priskv-utils.h"

#define TEST_COUNT 100

static const char *addr = "127.0.0.1";
static int port = 6379;
static priskvClusterClient *client;
static void *host_sendbuf, *host_recvbuf;
static priskvClusterMemory *host_sendmem, *host_recvmem;
static int clientfd;
static int epollfd;
static const uint32_t key_size = 64, value_size = 1024;
static char key[64], value[1024];

static int buffer_init()
{
    host_sendbuf = malloc(value_size);
    host_recvbuf = malloc(value_size);
    memset(host_sendbuf, 0, value_size);
    memset(host_recvbuf, 0, value_size);

    return 0;
}

static void buffer_deinit()
{
    free(host_sendbuf);
    free(host_recvbuf);
}

static int priskv_cluster_init()
{
    client = priskvClusterConnect(addr, port, "kvcache-redis");
    if (!client) {
        printf("Cannot connect to priskv cluster!\n");
        return -1;
    }

    printf("Connected to priskv cluster %s:%d!\n", addr, port);

    clientfd = priskvClusterClientGetFd(client);
    if (clientfd < 0) {
        printf("Cannot get fd from priskv cluster!\n");
        return -1;
    }

    host_sendmem = priskvClusterRegMemory(client, (uint64_t)host_sendbuf, value_size,
                                        (uint64_t)host_sendbuf, -1);
    if (!host_sendmem) {
        printf("Cannot register host send buffer!\n");
        return -1;
    }

    host_recvmem = priskvClusterRegMemory(client, (uint64_t)host_recvbuf, value_size,
                                        (uint64_t)host_recvbuf, -1);
    if (!host_recvmem) {
        printf("Cannot register host recv buffer!\n");
        return -1;
    }

    return 0;
}

static void priskv_cluster_deinit()
{
    priskvClusterDeregMemory(host_sendmem);
    priskvClusterDeregMemory(host_recvmem);
    priskvClusterClose(client);
}

static int epoll_init()
{
    struct epoll_event event = {0};

    epollfd = epoll_create1(0);
    if (epollfd < 0) {
        printf("Cannot create epoll!\n");
        return -1;
    }

    event.events = EPOLLIN | EPOLLET;
    event.data.fd = clientfd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, clientfd, &event) < 0) {
        printf("Cannot add connfd to epoll!\n");
        return -1;
    }

    return 0;
}

static void epoll_deinit()
{
    close(epollfd);
}

static void poller_wait(int timeout)
{
    struct epoll_event events[1];
    int nevents;

    nevents = epoll_wait(epollfd, events, 1, timeout);
    if (!nevents) {
        return;
    }

    if (nevents < 0) {
        assert(errno == EINTR);
        return;
    }

    priskvClusterClientProcess(client, 0);
}

static void priskv_cluster_req_cb(priskvClusterStatus status, uint32_t valuelen, void *cbarg)
{
    int *done = cbarg;

    if (status != PRISKV_CLUSTER_STATUS_OK) {
        printf("priskv cluster response: status[%d]\n", status);
        *done = -1;
    } else {
        *done = 1;
    }
}

static int wait_for_done(int *done, const char *op)
{
    while (!(*done)) {
        poller_wait(1000);
    }
    if ((*done) < 0) {
        printf("priskv %s failed!\n", op);
        return -1;
    }

    return 0;
}

#define REPORT(_job, _op, _i, _key, _value)                                                        \
    printf("%s[%d] (%s): OK!\n\tkey: %s\n\tvalue: %s\n", _op, _i, _job, _key, _value);

static int priskv_async_test()
{
    priskvClusterSGL sgl;
    int done;

    sgl.mem = NULL;

    for (int i = 0; i < TEST_COUNT; i++) {
        priskv_random_string((uint8_t *)key, key_size);
        priskv_random_string((uint8_t *)value, value_size);
        memcpy(host_sendbuf, value, value_size);

        done = 0;
        sgl.iova = (uint64_t)host_sendbuf;
        sgl.length = value_size;
        sgl.mem = host_sendmem;
        priskvClusterAsyncSet(client, key, &sgl, 1, PRISKV_CLUSTER_KEY_MAX_TIMEOUT, priskv_cluster_req_cb,
                            &done);
        assert(wait_for_done(&done, "SET") == 0);
        REPORT("async", "SET", i, key, value);

        done = 0;
        sgl.iova = (uint64_t)host_recvbuf;
        sgl.length = value_size;
        sgl.mem = host_recvmem;
        priskvClusterAsyncGet(client, key, &sgl, 1, priskv_cluster_req_cb, &done);
        assert(wait_for_done(&done, "GET") == 0);
        REPORT("async", "GET", i, key, (char *)host_recvbuf);

        /* Check data consistency */
        assert(memcmp(host_recvbuf, value, value_size) == 0);

        done = 0;
        priskvClusterAsyncDelete(client, key, priskv_cluster_req_cb, &done);
        assert(wait_for_done(&done, "DELETE") == 0);
        REPORT("async", "DELETE", i, key, value);
    }

    return 0;
}

static int priskv_sync_test()
{
    priskvClusterSGL sgl;
    uint32_t valuelen;

    sgl.mem = NULL;

    for (int i = 0; i < TEST_COUNT; i++) {
        priskv_random_string((uint8_t *)key, key_size);
        priskv_random_string((uint8_t *)value, value_size);
        memcpy(host_sendbuf, value, value_size);

        sgl.iova = (uint64_t)host_sendbuf;
        sgl.length = value_size;
        sgl.mem = host_sendmem;
        assert(priskvClusterSet(client, key, &sgl, 1, PRISKV_CLUSTER_KEY_MAX_TIMEOUT) ==
               PRISKV_CLUSTER_STATUS_OK);
        REPORT("sync", "SET", i, key, value);

        sgl.iova = (uint64_t)host_recvbuf;
        sgl.length = value_size;
        sgl.mem = host_recvmem;
        assert(priskvClusterGet(client, key, &sgl, 1, &valuelen) == PRISKV_CLUSTER_STATUS_OK);
        REPORT("sync", "GET", i, key, (char *)host_recvbuf);

        /* Check data consistency */
        assert(memcmp(host_recvbuf, value, value_size) == 0);

        assert(priskvClusterDelete(client, key) == PRISKV_CLUSTER_STATUS_OK);
        REPORT("sync", "DELETE", i, key, value);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    if (buffer_init()) {
        return -1;
    }

    if (priskv_cluster_init()) {
        return -1;
    }

    if (epoll_init()) {
        return -1;
    }

    if (priskv_async_test()) {
        return -1;
    }

    if (priskv_sync_test()) {
        return -1;
    }

    epoll_deinit();
    priskv_cluster_deinit();
    buffer_deinit();

    return 0;
}
