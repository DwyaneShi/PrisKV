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
#include <pthread.h>
#include <sys/time.h>
#include "buddy.h"
#include "priskv-utils.h"

#define NUM_THREADS 8
#define NUM_ITERATIONS 1024
#define NUM_MEM_BLOCKS 32
#define SIZE_RANGE 1024

typedef struct {
    uint32_t size;
    uint8_t *str;
} random_str;

typedef struct {
    void *buddy;
    int thread_id;
    random_str *random_strs;
} thread_args;

void *test_buddy_alloc(void *arg)
{
    thread_args *args = (thread_args *)arg;
    void *buddy = args->buddy;
    for (int iteration = 0; iteration < NUM_ITERATIONS; iteration++) {
        random_str *rs = &args->random_strs[args->thread_id + iteration];
        uint8_t *elems = priskv_buddy_alloc(buddy, rs->size);
        if (!elems) {
            printf("thread %ld iter %d buddy alloc fail\n", pthread_self(), iteration);
            pthread_exit(NULL);
        }

        memcpy(elems, rs->str, rs->size);
        assert(memcmp(elems, rs->str, rs->size) == 0);
        priskv_buddy_free(buddy, elems);
    }
    pthread_exit(NULL);
}

int main()
{
    uint32_t nmemb = NUM_MEM_BLOCKS;
    uint32_t size = SIZE_RANGE;
    void *buddy;
    uint8_t *base;
    struct timeval start, end;

    thread_args thread_args_array[NUM_THREADS];

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

    random_str *random_strs = malloc(sizeof(random_str) * NUM_THREADS * NUM_ITERATIONS);
    for (uint32_t i = 0; i < NUM_THREADS * NUM_ITERATIONS; i++) {
        srand(time(NULL) ^ i);
        random_strs[i].size = rand() % SIZE_RANGE + 1;
        random_strs[i].str = malloc(sizeof(uint8_t *) * random_strs[i].size);
        priskv_random_string(random_strs[i].str, random_strs[i].size);
    }

    pthread_t threads[NUM_THREADS];
    gettimeofday(&start, NULL);
    for (uint32_t i = 0; i < NUM_THREADS; i++) {
        thread_args_array[i] =
            (thread_args) {.buddy = buddy, .thread_id = i, .random_strs = random_strs};
        if (pthread_create(&threads[i], NULL, test_buddy_alloc, &thread_args_array[i]) != 0) {
            fprintf(stderr, "priskv test-buddy-mt create thread error\n");
            return 1;
        }
    }
    for (uint32_t i = 0; i < NUM_THREADS; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            fprintf(stderr, "priskv test-buddy-mt join thread error\n");
            return 1;
        }
    }
    gettimeofday(&end, NULL);
    printf("BUDDY Alloc: %d Threads, %d Iteration, %d MemBlocks, %d Size Cost[%ld us]\n",
           NUM_THREADS, NUM_ITERATIONS, nmemb, size, priskv_time_elapsed_us(&start, &end));

    for (uint32_t i = 0; i < NUM_THREADS * NUM_ITERATIONS; i++) {
        free(random_strs[i].str);
    }

    priskv_buddy_destroy(buddy);
    free(random_strs);
    free(base);

    return 0;
}
