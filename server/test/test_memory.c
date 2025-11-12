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
#include <errno.h>
#include <unistd.h>
#include <getopt.h>

#include "memory.h"
#include "priskv-log.h"

static priskv_log_level log_level = priskv_log_warn;
static bool tmpfs = true;

static const char *invfile = "./invalid-memory-file";
static uint16_t max_key_length = 128;
static uint32_t max_keys = 1024 * 128;
static uint32_t value_block_size = 256;
static uint64_t value_blocks = 1024 * 1024;

static void test_memory_exist_file()
{
    int fd, ret;

    /* does any remained test file? */
    unlink(invfile);
    fd = creat(invfile, 0600);
    assert(fd >= 0);

    ret = priskv_mem_create(invfile, 128, 1024, 4096, 1024, 0);
    assert(ret == -EEXIST);

    /* clean test file */
    unlink(invfile);

    printf("TEST MEM: exist file [OK]\n");
}

static void test_memory_invalid_fs()
{
    int ret;

    ret = priskv_mem_create("./invalid-memory-file", 128, 1024, 4096, 1024, 0);
    assert(ret == -ENODEV);

    printf("TEST MEM: invalid fs (not hugetlb/tmpfs) [OK]\n");
}

static void test_memory_file(char *path)
{
    uint64_t key_size = (sizeof(priskv_key) + max_key_length) * max_keys;
    uint64_t value_size = value_blocks * value_block_size;
    int ret;
    void *memfile;

    /* step 1, create a memory file */
    ret = priskv_mem_create(path, max_key_length, max_keys, value_block_size, value_blocks, 0);
    assert(ret == 0);

    /* step 2, load a memory file */
    memfile = priskv_mem_load(path);
    assert(memfile);

    uint8_t *key0 = priskv_mem_key_addr(memfile);
    uint8_t *value0 = priskv_mem_value_addr(memfile);

    memset(key0, 0xc5, key_size);
    priskv_log_debug("TEST-MEM: clear key [%p, %p]\n", key0, key0 + key_size);
    memset(value0, 0xc5, value_size);
    priskv_log_debug("TEST-MEM: clear value [%p, %p]\n", value0, value0 + value_size);
    priskv_mem_close(memfile);

    /* step 3, load the memory file again */
    memfile = priskv_mem_load(path);
    assert(memfile);

    uint8_t *key1 = priskv_mem_key_addr(memfile);
    uint8_t *value1 = priskv_mem_value_addr(memfile);
    assert(!priskv_memcmp64(key1, 0xc5, key_size));
    assert(!priskv_memcmp64(value1, 0xc5, value_size));
    priskv_mem_close(memfile);

    unlink(path);
}

static void test_memory_tmpfs()
{
    char path[256] = {0};
    int uid = getuid();

    if (uid) {
        snprintf(path, sizeof(path), "/run/user/%d/priskv-memory-file", getuid());
    } else {
        snprintf(path, sizeof(path), "/run/priskv-memory-file");
    }
    test_memory_file(path);

    printf("TEST MEM: tmpfs [OK]\n");
}

static void test_memory_hugetlb()
{
    char path[256] = {0};

    if (getuid()) {
        printf("TEST MEM: hugetlbfs (not root user) [SKIP]\n");
        return;
    }

    int fd = open("/sys/kernel/mm/hugepages/hugepages-2048kB/free_hugepages", O_RDONLY);
    if (fd == -1) {
        printf("TEST MEM: failed to detect hugepages [SKIP]\n");
        return;
    }

    char buf[32] = {0};
    if (read(fd, buf, sizeof(buf)) < 0) {
        printf("TEST MEM: failed to detect hugepages [SKIP]\n");
        close(fd);
        return;
    }

    if (!atoi(buf)) {
        printf("TEST MEM: 0 hugepages [SKIP]\n");
        close(fd);
        return;
    }

    snprintf(path, sizeof(path), "/dev/hugepages/priskv-memory-file");
    test_memory_file(path);

    printf("TEST MEM: hugetlbfs [OK]\n");
}

static void test_memory_anon()
{
    uint64_t key_size = (sizeof(priskv_key) + max_key_length) * max_keys;
    uint64_t value_size = value_blocks * value_block_size;
    void *memfile;

    memfile = priskv_mem_anon(max_key_length, max_keys, value_block_size, value_blocks, 1);
    assert(memfile);

    uint8_t *key0 = priskv_mem_key_addr(memfile);
    uint8_t *value0 = priskv_mem_value_addr(memfile);

    memset(key0, 0xc5, key_size);
    memset(value0, 0xc5, value_size);

    uint8_t *key1 = priskv_mem_key_addr(memfile);
    uint8_t *value1 = priskv_mem_value_addr(memfile);
    assert(!priskv_memcmp64(key1, 0xc5, key_size));
    assert(!priskv_memcmp64(value1, 0xc5, value_size));
    priskv_mem_close(memfile);

    printf("TEST MEM: anonymous memory [OK]\n");
}

static const char *test_memory_short_opts = "tl:h";
static struct option test_memory_long_opts[] = {
    {"no-tmpfs", no_argument, 0, 't'},
    {"log-level", required_argument, 0, 'l'},
    {"help", no_argument, 0, 'h'},
};

static void test_memory_showhelp()
{
    printf("  -t/--no-tmpfs   skip tmpfs test\n");
    printf("  -l/--log-level LEVEL  error, warn, notice[default], info or debug\n");
    printf("  -h/--help  show help\n");

    exit(0);
}

static void test_memory_parsr_arg(int argc, char *argv[])
{
    int args, ch;

    while (1) {
        ch = getopt_long(argc, argv, test_memory_short_opts, test_memory_long_opts, &args);
        if (ch == -1) {
            break;
        }

        switch (ch) {
        case 't':
            tmpfs = false;
            break;

        case 'l':
            if (!strcmp(optarg, "error")) {
                log_level = priskv_log_error;
            } else if (!strcmp(optarg, "warn")) {
                log_level = priskv_log_warn;
            } else if (!strcmp(optarg, "notice")) {
                log_level = priskv_log_notice;
            } else if (!strcmp(optarg, "info")) {
                log_level = priskv_log_info;
            } else if (!strcmp(optarg, "debug")) {
                log_level = priskv_log_debug;
            } else {
                test_memory_showhelp();
            }
            break;

        case 'h':
        default:
            test_memory_showhelp();
        }
    }
}

int main(int argc, char *argv[])
{
    test_memory_parsr_arg(argc, argv);

    priskv_set_log_level(log_level);

    test_memory_exist_file();
    test_memory_invalid_fs();
    if (tmpfs) {
        test_memory_tmpfs();
    }
    test_memory_hugetlb();
    test_memory_anon();

    return 0;
}
