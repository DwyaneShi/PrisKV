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
#include <string.h>
#include <pthread.h>
#include <sys/time.h>

#include "slab.h"
#include "priskv-utils.h"

#define OBJECT_SIZE 1024
#define NUM_OBJECTS (1024 * 1024)
#define NUM_THREADS 8

#define BITS_OF_UL (sizeof(uint64_t) * 8)

typedef struct {
    void *slab;
    int thread_id;
    uint8_t **objs;
    int *objs_map;
} thread_args;

void *test_slab_alloc(void *arg)
{
    thread_args *args = (thread_args *)arg;
    void *slab = args->slab;
    int *objs_map = args->objs_map;
    uint8_t **objs = args->objs;
    int thread_id = args->thread_id;
    int index;

    for (uint32_t i = 0; i < NUM_OBJECTS; i++) {
        objs[i + NUM_OBJECTS * thread_id] = priskv_slab_alloc(slab);
        index = priskv_slab_index(slab, objs[i + NUM_OBJECTS * thread_id]);
        priskv_atomic_inc(&objs_map[index]);
    }

    return NULL;
}

int main()
{
    uint32_t size = OBJECT_SIZE;
    uint32_t objects = NUM_OBJECTS * NUM_THREADS;
    const char *name = "test-slab-mt";
    void *slab;
    uint8_t *base, *ptr;
    struct timeval start, end;

    thread_args thread_args_array[NUM_THREADS];
    uint8_t **objs = malloc(sizeof(uint8_t *) * objects);
    int *objs_map = malloc(sizeof(int) * objects);

    for (int i = 0; i < objects; i++) {
        objs_map[i] = 0;
    }

    base = calloc(objects, size);
    assert(base);

    slab = priskv_slab_create(name, base, size, objects);
    assert(slab);
    assert(priskv_slab_base(slab) == base);
    assert(priskv_slab_size(slab) == size);
    assert(priskv_slab_objects(slab) == objects);
    assert(!strcmp(priskv_slab_name(slab), name));

    gettimeofday(&start, NULL);
    pthread_t threads[NUM_THREADS];
    for (uint32_t i = 0; i < NUM_THREADS; i++) {
        thread_args_array[i] =
            (thread_args) {.slab = slab, .objs_map = objs_map, .objs = objs, .thread_id = i};
        if (pthread_create(&threads[i], NULL, test_slab_alloc, &thread_args_array[i]) != 0) {
            fprintf(stderr, "priskv test-slab-mt create thread error\n");
            return 1;
        }
    }

    for (uint32_t i = 0; i < NUM_THREADS; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            fprintf(stderr, "priskv test-slab-mt join thread error\n");
            return 1;
        }
    }
    gettimeofday(&end, NULL);

    printf("SLAB Alloc: %d Threads, %d Objects, %d Size Cost[%ld us]\n", NUM_THREADS, NUM_OBJECTS,
           OBJECT_SIZE, priskv_time_elapsed_us(&start, &end));

    uint64_t *bitmap = priskv_slab_bitmap(slab);
    uint32_t bitmap_length = ALIGN_UP(objects, BITS_OF_UL) / BITS_OF_UL;
    for (uint32_t i = 0; i < bitmap_length; i++) {
        assert(bitmap[i] == 0);
    }

    for (uint32_t i = 0; i < objects; i++) {
        assert(objs_map[i] == 1);
    }

    for (uint32_t i = 0; i < objects; i++) {
        ptr = objs[i];
        priskv_slab_free(slab, ptr);
    }

    free(objs);
    free(objs_map);
    priskv_slab_destroy(slab);
    free(base);
    return 0;
}
