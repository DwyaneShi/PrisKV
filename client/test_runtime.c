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
#include <cuda.h>
#include <cuda_runtime.h>
#include <sys/epoll.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "priskv.h"

#define BASE_STRING "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
#define BASE_LEN 62
#define TARGET_SIZE (512 * 1024)

static const char *raddr = "fdbd:dc0c:2:726::15";
static int rport = 9000;
static priskv_client *client;
static priskv_memory *dev_sendmem, *dev_recvmem;
static void *dev_sendbuf, *dev_recvbuf;
static priskv_memory *host_sendmem, *host_recvmem;
static void *host_sendbuf, *host_recvbuf;
static int connfd;
static int epollfd;
static const char key[] = "my_key";

static void print_time_interval(const char *msg, struct timeval *current, struct timeval *prev)
{
    long seconds = current->tv_sec - prev->tv_sec;
    long useconds = current->tv_usec - prev->tv_usec;
    if (useconds < 0) {
        seconds--;
        useconds += 1000000;
    }
    double interval = seconds + useconds / 1000000.0;
    printf("[Time] %s at %ld.%06ld (interval: %.6f s)\n", msg, (long)current->tv_sec,
           (long)current->tv_usec, interval);
}

static int gdr_is_support(void)
{
    CUdevice currentDev;
    int cudaDev;
    static int support_gdr = -1;
    cudaError_t err;

    if (support_gdr >= 0) {
        return support_gdr;
    }

    err = cudaGetDevice(&cudaDev);
    if (err != cudaSuccess) {
        support_gdr = 0;
        return support_gdr;
    }

    err = cuDeviceGet(&currentDev, cudaDev);
    if (err != cudaSuccess) {
        support_gdr = 0;
        return support_gdr;
    }

    err = cuDeviceGetAttribute(&support_gdr, CU_DEVICE_ATTRIBUTE_GPU_DIRECT_RDMA_SUPPORTED,
                               currentDev);
    if (err != cudaSuccess) {
        support_gdr = 0;
        return support_gdr;
    }

    printf("Cuda(%d) support GDR: %s\n", (int)currentDev, support_gdr ? "yes" : "no");

    return support_gdr;
}

static int buffer_init(char *value, uint32_t value_size)
{
    assert(gdr_is_support());

    struct timeval start, end;
    struct timeval gpu_malloc_1_start, gpu_malloc_2_start;
    struct timeval cpu_malloc_1_start, cpu_malloc_2_start;
    struct timeval cpu_to_cpu_1_start, cpu_to_cpu_2_start;
    struct timeval cpu_to_gpu_start, gpu_to_gpu_start;

    struct timeval prev_time;
    gettimeofday(&prev_time, NULL);
    printf("[Time] Malloc GPU before entered at %ld.%06ld\n", (long)prev_time.tv_sec,
           (long)prev_time.tv_usec);

    // === GPU Memory Allocation (1st) ===
    gettimeofday(&gpu_malloc_1_start, NULL);
    if (cudaMalloc(&dev_sendbuf, value_size) != cudaSuccess) {
        printf("Cannot allocate send buffer!\n");
        return -1;
    }
    gettimeofday(&end, NULL);
    print_time_interval("GPU Malloc 1st", &end, &gpu_malloc_1_start);

    // === GPU Memory Allocation (2nd) ===
    gettimeofday(&gpu_malloc_2_start, NULL);
    if (cudaMalloc(&dev_recvbuf, value_size) != cudaSuccess) {
        printf("Cannot allocate recv buffer!\n");
        return -1;
    }
    gettimeofday(&end, NULL);
    print_time_interval("GPU Malloc 2nd", &end, &gpu_malloc_2_start);

    // === GPU Memory Initialization ===
    gettimeofday(&start, NULL);
    cudaMemset(dev_sendbuf, 0, value_size);
    cudaMemset(dev_recvbuf, 0, value_size);
    gettimeofday(&end, NULL);
    print_time_interval("GPU memset", &end, &start);

    // === CPU to GPU Copy ===
    gettimeofday(&cpu_to_gpu_start, NULL);
    cudaMemcpy(dev_sendbuf, value, value_size, cudaMemcpyHostToDevice);
    gettimeofday(&end, NULL);
    print_time_interval("CPU-GPU memcpy", &end, &cpu_to_gpu_start);

    // === CPU Memory Allocation (1st) ===
    gettimeofday(&cpu_malloc_1_start, NULL);
    host_sendbuf = malloc(value_size);
    gettimeofday(&end, NULL);
    print_time_interval("CPU Malloc 1st", &end, &cpu_malloc_1_start);

    // === CPU Memory Allocation (2nd) ===
    gettimeofday(&cpu_malloc_2_start, NULL);
    host_recvbuf = malloc(value_size);
    gettimeofday(&end, NULL);
    print_time_interval("CPU Malloc 2nd", &end, &cpu_malloc_2_start);

    // === CPU Memory Initialization ===
    gettimeofday(&start, NULL);
    memset(host_sendbuf, 0, value_size);
    memset(host_recvbuf, 0, value_size);
    gettimeofday(&end, NULL);
    print_time_interval("CPU memset", &end, &start);

    // === CPU to CPU Copy (1st) ===
    gettimeofday(&cpu_to_cpu_1_start, NULL);
    memcpy(host_sendbuf, value, value_size);
    gettimeofday(&end, NULL);
    print_time_interval("CPU-CPU memcpy 1st", &end, &cpu_to_cpu_1_start);

    // === CPU to CPU Copy (2nd) ===
    gettimeofday(&cpu_to_cpu_2_start, NULL);
    memcpy(host_recvbuf, host_sendbuf, value_size);
    gettimeofday(&end, NULL);
    print_time_interval("CPU-CPU memcpy 2nd", &end, &cpu_to_cpu_2_start);

    // === GPU to GPU Copy ===
    gettimeofday(&gpu_to_gpu_start, NULL);
    cudaMemcpy(dev_recvbuf, dev_sendbuf, value_size, cudaMemcpyDeviceToDevice);
    gettimeofday(&end, NULL);
    print_time_interval("GPU-GPU memcpy", &end, &gpu_to_gpu_start);

    // === Final Reset ===
    cudaMemset(dev_recvbuf, 0, value_size);
    memset(host_recvbuf, 0, value_size);

    return 0;
}

static void buffer_deinit()
{
    cudaFree(dev_sendbuf);
    cudaFree(dev_recvbuf);

    free(host_sendbuf);
    free(host_recvbuf);
}

static int priskv_init(uint32_t value_size)
{
    // === 初始化客户端连接 ===
    client = priskv_connect(raddr, rport, NULL, 0, 0);
    if (!client) {
        printf("Cannot connect to priskv server!\n");
        return -1;
    }

    printf("Connected to priskv server!\n");

    struct timeval start, end;

    // === 内存注册前时间戳 ===
    struct timeval prev_time;
    gettimeofday(&prev_time, NULL);
    printf("[Time] Register Memory before entered at %ld.%06ld\n", (long)prev_time.tv_sec,
           (long)prev_time.tv_usec);

    // === GPU Send Buffer 注册 ===
    gettimeofday(&start, NULL);
    dev_sendmem =
        priskv_reg_memory(client, (uint64_t)dev_sendbuf, value_size, (uint64_t)dev_sendbuf, -1);
    if (!dev_sendmem) {
        printf("Cannot register device send buffer!\n");
        return -1;
    }
    gettimeofday(&end, NULL);
    print_time_interval("Register GPU Send Buffer", &end, &start);

    // === GPU Recv Buffer 注册 ===
    gettimeofday(&start, NULL);
    dev_recvmem =
        priskv_reg_memory(client, (uint64_t)dev_recvbuf, value_size, (uint64_t)dev_recvbuf, -1);
    if (!dev_recvmem) {
        printf("Cannot register device recv buffer!\n");
        return -1;
    }
    gettimeofday(&end, NULL);
    print_time_interval("Register GPU Recv Buffer", &end, &start);

    // === CPU Send Buffer 注册 ===
    gettimeofday(&start, NULL);
    host_sendmem =
        priskv_reg_memory(client, (uint64_t)host_sendbuf, value_size, (uint64_t)host_sendbuf, -1);
    if (!host_sendmem) {
        printf("Cannot register host send buffer!\n");
        return -1;
    }
    gettimeofday(&end, NULL);
    print_time_interval("Register CPU Send Buffer", &end, &start);

    // === CPU Recv Buffer 注册 ===
    gettimeofday(&start, NULL);
    host_recvmem =
        priskv_reg_memory(client, (uint64_t)host_recvbuf, value_size, (uint64_t)host_recvbuf, -1);
    if (!host_recvmem) {
        printf("Cannot register host recv buffer!\n");
        return -1;
    }
    gettimeofday(&end, NULL);
    print_time_interval("Register CPU Recv Buffer", &end, &start);

    // === 获取文件描述符 ===
    connfd = priskv_get_fd(client);
    if (connfd < 0) {
        printf("Cannot get fd from priskv connection!\n");
        return -1;
    }

    return 0;
}

static void priskv_deinit()
{
    priskv_dereg_memory(dev_sendmem);
    priskv_dereg_memory(dev_recvmem);

    priskv_dereg_memory(host_sendmem);
    priskv_dereg_memory(host_recvmem);
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
    event.data.fd = connfd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, connfd, &event) < 0) {
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

    priskv_process(client, events[0].events);
}

static void priskv_req_cb(uint64_t request_id, priskv_status status, void *result)
{
    int *done = (int *)request_id;

    if (status != PRISKV_STATUS_OK) {
        printf("priskv response: status[%d]\n", status);
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

#define REPORT(_job, _op, _key) printf("(%s) [%s]: OK!\n\tkey: %s\n\t\n", _job, _op, _key);

static int priskv_async_test(char *value, uint32_t value_size)
{
    priskv_sgl sgl;
    int done = 0;
    struct timeval start, end;

    // === GPU Memory: SET 操作 ===
    gettimeofday(&start, NULL);
    sgl.iova = (uint64_t)dev_sendbuf;
    sgl.length = value_size;
    sgl.mem = dev_sendmem;
    priskv_set_async(client, key, &sgl, 1, PRISKV_KEY_MAX_TIMEOUT, (uint64_t)&done, priskv_req_cb);
    assert(wait_for_done(&done, "SET") == 0);
    gettimeofday(&end, NULL);
    print_time_interval("GPU SET Operation", &end, &start);

    // === GPU Memory: GET 操作 ===
    done = 0;
    cudaMemset(dev_recvbuf, 0, value_size);
    gettimeofday(&start, NULL);
    priskv_get_async(client, key, &sgl, 1, (uint64_t)&done, priskv_req_cb);
    assert(wait_for_done(&done, "GET") == 0);
    gettimeofday(&end, NULL);
    print_time_interval("GPU GET Operation", &end, &start);

    // === GPU Memory: DELETE 操作 ===
    gettimeofday(&start, NULL);
    done = 0;
    priskv_delete_async(client, key, (uint64_t)&done, priskv_req_cb);
    assert(wait_for_done(&done, "DELETE") == 0);
    gettimeofday(&end, NULL);
    print_time_interval("GPU DELETE Operation", &end, &start);

    // === CPU Memory: SET 操作 ===
    gettimeofday(&start, NULL);
    done = 0;
    sgl.iova = (uint64_t)host_sendbuf;
    sgl.length = value_size;
    sgl.mem = host_sendmem;
    priskv_set_async(client, key, &sgl, 1, PRISKV_KEY_MAX_TIMEOUT, (uint64_t)&done, priskv_req_cb);
    assert(wait_for_done(&done, "SET") == 0);
    gettimeofday(&end, NULL);
    print_time_interval("CPU SET Operation", &end, &start);

    // === CPU Memory: GET 操作 ===
    done = 0;
    memset(host_recvbuf, 0, value_size);
    gettimeofday(&start, NULL);
    sgl.iova = (uint64_t)host_recvbuf;
    sgl.length = value_size;
    sgl.mem = host_recvmem;
    priskv_get_async(client, key, &sgl, 1, (uint64_t)&done, priskv_req_cb);
    assert(wait_for_done(&done, "GET") == 0);
    gettimeofday(&end, NULL);
    print_time_interval("CPU GET Operation", &end, &start);

    // === CPU Memory: DELETE 操作 ===
    gettimeofday(&start, NULL);
    done = 0;
    priskv_delete_async(client, key, (uint64_t)&done, priskv_req_cb);
    assert(wait_for_done(&done, "DELETE") == 0);
    gettimeofday(&end, NULL);
    print_time_interval("CPU DELETE Operation", &end, &start);

    // === CPU Memory: SET 操作（重复测试） ===
    gettimeofday(&start, NULL);
    done = 0;
    sgl.iova = (uint64_t)host_sendbuf;
    sgl.length = value_size;
    sgl.mem = host_sendmem;
    priskv_set_async(client, key, &sgl, 1, PRISKV_KEY_MAX_TIMEOUT, (uint64_t)&done, priskv_req_cb);
    assert(wait_for_done(&done, "SET") == 0);
    gettimeofday(&end, NULL);
    print_time_interval("CPU SET Operation (Repeat)", &end, &start);

    // === CPU Memory: GET 操作（重复测试） ===
    done = 0;
    memset(host_recvbuf, 0, value_size);
    gettimeofday(&start, NULL);
    sgl.iova = (uint64_t)host_recvbuf;
    sgl.length = value_size;
    sgl.mem = host_recvmem;
    priskv_get_async(client, key, &sgl, 1, (uint64_t)&done, priskv_req_cb);
    assert(wait_for_done(&done, "GET") == 0);
    gettimeofday(&end, NULL);
    print_time_interval("CPU GET Operation (Repeat)", &end, &start);

    // === GPU Memory: SET 操作（重复测试） ===
    gettimeofday(&start, NULL);
    done = 0;
    sgl.iova = (uint64_t)dev_sendbuf;
    sgl.length = value_size;
    sgl.mem = dev_sendmem;
    priskv_set_async(client, key, &sgl, 1, PRISKV_KEY_MAX_TIMEOUT, (uint64_t)&done, priskv_req_cb);
    assert(wait_for_done(&done, "SET") == 0);
    gettimeofday(&end, NULL);
    print_time_interval("GPU SET Operation (Repeat)", &end, &start);

    // === GPU Memory: GET 操作（重复测试） ===
    done = 0;
    cudaMemset(dev_recvbuf, 0, value_size);
    gettimeofday(&start, NULL);
    sgl.iova = (uint64_t)dev_recvbuf;
    sgl.length = value_size;
    sgl.mem = dev_recvmem;
    priskv_get_async(client, key, &sgl, 1, (uint64_t)&done, priskv_req_cb);
    assert(wait_for_done(&done, "GET") == 0);
    gettimeofday(&end, NULL);
    print_time_interval("GPU GET Operation (Repeat)", &end, &start);

    return 0;
}

int main(int argc, char *argv[])
{
    char *value = (char *)malloc(TARGET_SIZE + 1); // +1 用于终止符 '\0'
    if (!value) {
        perror("Memory allocation failed");
        return 1;
    }

    for (int i = 0; i < TARGET_SIZE; i += BASE_LEN) {
        memcpy(value + i, BASE_STRING, BASE_LEN);
    }
    value[TARGET_SIZE] = '\0';

    printf("Size of value: %d bytes\n", (int)strlen(value));
    uint32_t value_size = TARGET_SIZE + 1;

    if (buffer_init(value, value_size)) {
        return -1;
    }

    if (priskv_init(value_size)) {
        return -1;
    }

    if (epoll_init()) {
        return -1;
    }

    if (priskv_async_test(value, value_size)) {
        return -1;
    }

    epoll_deinit();
    priskv_deinit();
    buffer_deinit();

    free(value);

    return 0;
}