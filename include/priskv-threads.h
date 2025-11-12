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

#ifndef __PRISKV_THREADS__
#define __PRISKV_THREADS__

#include <stdint.h>

#if defined(__cplusplus)
extern "C"
{
#endif

#define PRISKV_THREAD_BUSY_POLL (1 << 0)

typedef struct priskv_thread priskv_thread;

typedef void (*priskv_thread_init_cb)(priskv_thread *thd, void *arg);
typedef void (*priskv_thread_cleanup_cb)(priskv_thread *thd, void *arg);

typedef struct priskv_thread_hooks {
    priskv_thread_init_cb init;
    priskv_thread_cleanup_cb cleanup;
    void *arg;
} priskv_thread_hooks;

int priskv_thread_call_function(priskv_thread *thread, int (*func)(void *arg), void *arg);
void priskv_thread_submit_function(priskv_thread *thread, int (*func)(void *arg), void *arg);
int priskv_thread_add_event_handler(priskv_thread *thread, int fd);
int priskv_thread_del_event_handler(priskv_thread *thread, int fd);

void priskv_thread_set_user_data(priskv_thread *thread, void *user_data);
void *priskv_thread_get_user_data(priskv_thread *thread);
int priskv_thread_get_epollfd(priskv_thread *thread);

typedef struct priskv_threadpool priskv_threadpool;
priskv_threadpool *priskv_threadpool_create(const char *prefix, int niothread, int nbgthread,
                                        int flags);
priskv_threadpool *priskv_threadpool_create_with_hooks(const char *prefix, int niothread, int nbgthread,
                                                   int flags,
                                                   const struct priskv_thread_hooks *hooks);

/* Threadpool iteration for per-thread initialization */
typedef int (*priskv_threadpool_iter_cb)(priskv_thread *thread, void *arg);
int priskv_threadpool_for_each_iothread(priskv_threadpool *pool, priskv_threadpool_iter_cb cb, void *arg);
void priskv_threadpool_destroy(priskv_threadpool *pool);
priskv_thread *priskv_threadpool_get_iothread(priskv_threadpool *pool, int index);
priskv_thread *priskv_threadpool_get_bgthread(priskv_threadpool *pool, int index);
priskv_thread *priskv_threadpool_find_iothread(priskv_threadpool *pool);
priskv_thread *priskv_threadpool_find_bgthread(priskv_threadpool *pool);

#if defined(__cplusplus)
}
#endif

#endif /* __PRISKV_THREADS__ */
