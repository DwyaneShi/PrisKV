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
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <getopt.h>
#include <sys/time.h>
#include <inttypes.h>
#include <stdbool.h>
#include <valkey/valkey.h>
#include <valkey/rdma.h>

#include "priskv-utils.h"
static char *address;
static int port = 6379;
static uint16_t g_key_length = 256;
static uint32_t g_value_length = 4096;
static uint64_t g_runtime = 0;
static bool enable_rdma = false;

typedef struct job_context job_context;
typedef int (*action_handler)(job_context *job);
static int valkey_action_get(job_context *job);
static int valkey_action_set(job_context *job);
static void job_fill(job_context *job);
static void job_progress(job_context *job);

typedef enum {
    VALKEY_OP_GET = 0,
    VALKEY_OP_SET,
    VALKEY_OP_MAX,
} valkey_op;

typedef struct {
    const char *name;
    action_handler action;
} valkey_op_action;
valkey_op g_op = VALKEY_OP_GET;
const valkey_op_action op_action[] = {[VALKEY_OP_GET] = {"get", valkey_action_get},
                                      [VALKEY_OP_SET] = {"set", valkey_action_set},
                                      [VALKEY_OP_MAX] = {"unknown", NULL}};

static valkey_op valkey_get_op(const char *op_str)
{
    int i;
    for (i = 0; i < VALKEY_OP_MAX; i++) {
        if (!strcmp(op_str, op_action[i].name)) {
            return i;
        }
    }

    return VALKEY_OP_MAX;
}

static const char *valkey_get_op_str(valkey_op op)
{
    if (op < 0 || op > VALKEY_OP_MAX) {
        op = VALKEY_OP_MAX;
    }
    return op_action[op].name;
}

static action_handler valkey_get_op_action(valkey_op op)
{
    if (op < 0 || op > VALKEY_OP_MAX) {
        op = VALKEY_OP_MAX;
    }
    return op_action[op].action;
}

typedef enum {
    VALKEY_MEM_TYPE_CPU = 0,
    VALKEY_MEM_TYPE_GPU,
    VALKEY_MEM_TYPE_MAX,
} valkey_mem_type;
valkey_mem_type g_mem_type = VALKEY_MEM_TYPE_CPU;
const char *mem_type_string[] = {[VALKEY_MEM_TYPE_CPU] = "cpu", [VALKEY_MEM_TYPE_GPU] = "gpu"};
static valkey_mem_type valkey_get_mem_type(const char *mem_type_str)
{
    int i;
    for (i = 0; i < VALKEY_MEM_TYPE_MAX; i++) {
        if (!strcmp(mem_type_str, mem_type_string[i])) {
            return i;
        }
    }
    return VALKEY_MEM_TYPE_MAX;
}

struct job_context {
    valkeyContext *valkey_context;
    int exit;
    uint64_t req_count;
    uint64_t err_cnt;
    int prepare_get_env;
    void *common_key;
    uint32_t common_key_len;
    void *common_value;
    uint32_t common_value_len;
    const char *op_name;
    action_handler op_action;
    void *(*alloc)(uint32_t size);
    void (*free)(void *ptr);
    void (*memset)(void *ptr, int value, uint32_t size);
    void *(*key_alloc)(job_context *job, uint32_t *len);
    void (*key_free)(job_context *job);
    void *(*value_alloc)(job_context *job, uint32_t *len);
    void (*value_free)(job_context *job);
    uint64_t first_ns;
    uint64_t last_ns;
};

static void valkey_showhelp(void)
{
    printf("Usage:\n");
    printf("  -p/--server-port PORT     \n      server port\n");
    printf("  -a/--server-addr ADDR     \n      server address\n");
    printf("  -o/--operator [set/get]   \n");
    printf("  -k/--key-length BYTES     \n      the length of KEY in bytes\n");
    printf("  -v/--value-length BYTES   \n      the length of VALUE in bytes, "
           "must be power of 2\n");
    printf("  -d/--iodepth DEPTH        \n      the count of concurrent requests\n");
    printf("  -m/--mem-type [gpu/cpu]   \n      the position of buffer\n");
    printf("  -t/--runtime SECENDS      \n      runtime in secends\n");
    printf("  -e/--enable_rdma 1      \n      weather or not enable rdma\n");
    exit(0);
}
static const char *valkey_short_opts = "p:a:o:k:v:d:m:t:e:";
static struct option valkey_long_opts[] = {
    {"server-port", required_argument, 0, 'p'},  {"server-addr", required_argument, 0, 'a'},
    {"operator", required_argument, 0, 'o'},     {"key-length", required_argument, 0, 'k'},
    {"value-length", required_argument, 0, 'v'}, {"iodepth", required_argument, 0, 'd'},
    {"mem-type", required_argument, 0, 'm'},     {"runtime", required_argument, 0, 't'},
    {"enable-rdma", required_argument, 0, 'e'},
};

static int valkey_parse_arg(int argc, char *argv[])
{
    int args, ch;
    int ret = 0;

    while (1) {
        ch = getopt_long(argc, argv, valkey_short_opts, valkey_long_opts, &args);
        if (ch == -1) {
            break;
        }
        switch (ch) {
        case 'a':
            address = optarg;
            break;
        case 'p':
            port = atoi(optarg);
            break;
        case 'o':
            g_op = valkey_get_op(optarg);
            if (g_op == VALKEY_OP_MAX) {
                printf("unknown operator\n");
                valkey_showhelp();
                ret = -1;
            }
            break;
        case 'k':
            g_key_length = atoi(optarg);
            break;
        case 'v':
            g_value_length = atoi(optarg);
            if (!IS_POWER_OF_2(g_value_length)) {
                printf("-v/--value_length must be power of 2\n");
                valkey_showhelp();
                ret = -1;
            }
            break;
        case 'm':
            g_mem_type = valkey_get_mem_type(optarg);
            if (g_mem_type == VALKEY_MEM_TYPE_MAX) {
                printf("unknown memory type\n");
                valkey_showhelp();
                ret = -1;
            }
            break;
        case 't':
            g_runtime = atoi(optarg);
            break;
        case 'e':
            enable_rdma = atoi(optarg) == 1 ? true : false;
            break;
        default:
            valkey_showhelp();
            ret = -1;
        }
    }
    return ret;
}

static void *cmem_alloc(uint32_t size)
{
    return malloc(size);
}

static void cmem_free(void *ptr)
{
    free(ptr);
}

static void cmem_memset(void *ptr, int value, uint32_t count)
{
    memset(ptr, value, count);
}

static void *gmem_alloc(uint32_t size)
{
    void *ptr;
    cudaError_t err;
    err = cudaMalloc(&ptr, size);
    if (err != cudaSuccess) {
        return NULL;
    }
    return ptr;
}

static void gmem_free(void *ptr)
{
    assert(cudaFree(ptr) == cudaSuccess);
}

static void gmem_memset(void *ptr, int value, uint32_t count)
{
    assert(cudaMemset(ptr, value, count) == cudaSuccess);
}

static void *alloc_common_key(job_context *job, uint32_t *len)
{
    if (job->common_key) {
        *len = job->common_key_len;
        return job->common_key;
    }
    job->common_key_len = g_key_length;
    job->common_key = malloc(job->common_key_len);
    assert(job->common_key);
    memset(job->common_key, 1, g_key_length);
    *len = job->common_key_len;
    return job->common_key;
}

static void free_common_key(job_context *job)
{
}

static void *alloc_common_value(job_context *job, uint32_t *len)
{
    if (job->common_value) {
        *len = job->common_value_len;
        return job->common_value;
    }
    job->common_value_len = g_value_length;
    job->common_value = job->alloc(job->common_value_len);
    assert(job->common_value);
    job->memset(job->common_value, 1, job->common_value_len);
    *len = job->common_value_len;
    return job->common_value;
}

static void free_common_value(job_context *job)
{
}

static int valkey_get_sync(valkeyContext *c, void *key, size_t key_len, void *value,
                           size_t value_len)
{
    valkeyReply *reply;
    void *gpu_value;
    reply = valkeyCommand(c, "GET %b", key, key_len);
    if (reply == NULL) {
        printf("NULL reply from server (error: %s)", c->errstr);
        return -1;
    }

    if (reply->type == VALKEY_REPLY_ERROR) {
        printf("Server Error: %s\n", reply->str);
        return -1;
    }

    if (g_mem_type == VALKEY_MEM_TYPE_GPU) {
        gpu_value = malloc(value_len);
        assert(gpu_value);
        cudaMemcpy(gpu_value, value, value_len, cudaMemcpyHostToDevice);
        cudaDeviceSynchronize();
        cudaFree(gpu_value);
    }
    return 0;
}

static int valkey_set_sync(valkeyContext *c, void *key, size_t key_len, void *value,
                           size_t value_len)
{
    valkeyReply *reply;

    reply = valkeyCommand(c, "SET %b %b", key, key_len, value, value_len);
    if (reply == NULL) {
        printf("NULL reply from server (error: %s)", c->errstr);
        return -1;
    }

    if (reply->type == VALKEY_REPLY_ERROR) {
        printf("Server Error: %s\n", reply->str);
        return -1;
    }

    return 0;
}

static int valkey_action_get(job_context *job)
{
    uint32_t key_len = 0;
    void *key = job->key_alloc(job, &key_len);

    if (!job->prepare_get_env) {
        job->prepare_get_env = 1;
        valkey_action_set(job);
    }
    return valkey_get_sync(job->valkey_context, key, key_len, job->common_value,
                           job->common_value_len);
}

static int valkey_action_set(job_context *job)
{
    uint32_t key_len = 0;
    uint32_t value_len = 0;
    void *host_value;

    void *key = job->key_alloc(job, &key_len);
    if (key == NULL) {
        printf("key alloc failed \n");
        exit(1);
    }

    void *value = job->value_alloc(job, &value_len);
    if (value == NULL) {
        printf("value alloc failed \n");
        exit(1);
    }

    if (g_mem_type == VALKEY_MEM_TYPE_GPU) {
        host_value = malloc(value_len);
        assert(host_value);
        cudaMemcpy(host_value, value, value_len, cudaMemcpyDeviceToHost);
        cudaDeviceSynchronize();
    }

    return valkey_set_sync(job->valkey_context, key, key_len,
                           g_mem_type == VALKEY_MEM_TYPE_GPU ? host_value : value, value_len);
}

static uint64_t get_clock_ns(void)
{
    int res;
    uint64_t ns;
    struct timeval tv;

    res = gettimeofday(&tv, NULL);
    ns = tv.tv_sec * 1000000000ULL + tv.tv_usec * 1000;
    if (res == -1) {
        fprintf(stderr, "could not get requested clock\n");
        exit(10);
    }

    return ns;
}

static int job_init(job_context *job)
{
    valkeyContext *c;
    valkeyOptions options = {0};

    if (enable_rdma) {
        valkeyInitiateRdma();
        VALKEY_OPTIONS_SET_RDMA(&options, address, port);
    } else {
        VALKEY_OPTIONS_SET_TCP(&options, address, port);
    }

    c = valkeyConnectWithOptions(&options);
    if (c == NULL || c->err) {
        if (c) {
            printf("Connection error: %s\n", c->errstr);
            valkeyFree(c);
        } else {
            printf("Connection error: can't allocate valkey context\n");
        }
        exit(1);
    }

    memset(job, 0, sizeof(job_context));
    job->valkey_context = c;
    switch (g_mem_type) {
    case VALKEY_MEM_TYPE_CPU:
        job->alloc = cmem_alloc;
        job->free = cmem_free;
        job->memset = cmem_memset;
        break;
    case VALKEY_MEM_TYPE_GPU:
        job->alloc = gmem_alloc;
        job->free = gmem_free;
        job->memset = gmem_memset;
        break;
    default:
        assert(0);
        break;
    }

    job->key_alloc = alloc_common_key;
    job->key_free = free_common_key;
    job->value_alloc = alloc_common_value;
    job->value_free = free_common_value;
    job->op_name = valkey_get_op_str(g_op);
    job->op_action = valkey_get_op_action(g_op);
    job->req_count = 0;
    job->last_ns = job->first_ns = get_clock_ns();
    job->exit = 0;
    job->err_cnt = 0;

    return 0;
}

static void job_fill(job_context *job)
{
    int ret = 0;
    ret = job->op_action(job);

    if (ret) {
        job->err_cnt++;
        return;
    }
    job->req_count++;
}

static void job_progress(job_context *job)
{
    uint64_t now = get_clock_ns();
    uint64_t qps_avg;
    uint64_t lat_avg_us;

    if (now - job->last_ns < 1000000000ULL) {
        return;
    }

    if (now - job->first_ns >= g_runtime * 1000000000ULL) {
        job->exit = 1;
    }

    qps_avg = 1000000000.0 * job->req_count / (now - job->first_ns);
    lat_avg_us = (now - job->first_ns) / (1000.0 * job->req_count);
    job->last_ns = now;
    printf("[%s]average qps %" PRIu64 ", latency %" PRIu64 " us\n", job->op_name, qps_avg,
           lat_avg_us);
}

static int job_run(job_context *job)
{
    while (!job->exit && !job->err_cnt) {
        job_fill(job);
        job_progress(job);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    job_context job;

    if (valkey_parse_arg(argc, argv)) {
        return -1;
    }

    job_init(&job);
    job_run(&job);

    return 0;
}