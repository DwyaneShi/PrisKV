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

#include <sys/eventfd.h>
#include <pthread.h>
#include <stdlib.h>

#include "priskv-workqueue.h"
#include "priskv-event.h"
#include "list.h"

typedef struct priskv_work priskv_work;

struct priskv_work {
    struct list_node node;
    int (*func)(void *arg);
    void *arg;
    int retval;
    bool sync;
    bool done;
    pthread_cond_t wait;
    pthread_mutex_t mutex;
};

struct priskv_workqueue {
    pthread_spinlock_t works_lock;
    struct list_head works;
    int eventfd;
};

static inline void priskv_workqueue_kick(priskv_workqueue *wq)
{
    uint64_t u = 1;

    write(wq->eventfd, &u, sizeof(u));
}

static inline void priskv_workqueue_ack(priskv_workqueue *wq)
{
    uint64_t u = 1;

    read(wq->eventfd, &u, sizeof(u));
}

static void priskv_workqueue_process(int fd, void *opaque, uint32_t events)
{
    priskv_workqueue *wq = opaque;
    priskv_work *work, *tmp;

    assert(fd == wq->eventfd);
    priskv_workqueue_ack(wq);

    pthread_spin_lock(&wq->works_lock);
    list_for_each_safe (&wq->works, work, tmp, node) {
        list_del(&work->node);
        pthread_spin_unlock(&wq->works_lock);

        /* do one work */
        work->retval = work->func(work->arg);

        if (work->sync) {
            pthread_mutex_lock(&work->mutex);
            work->done = true;
            pthread_cond_signal(&work->wait);
            pthread_mutex_unlock(&work->mutex);
        } else {
            free(work);
        }

        pthread_spin_lock(&wq->works_lock);
    }
    pthread_spin_unlock(&wq->works_lock);
}

priskv_workqueue *priskv_workqueue_create(int epollfd)
{
    priskv_workqueue *wq = malloc(sizeof(priskv_workqueue));
    if (wq == NULL) {
        return NULL;
    }

    wq->eventfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    assert(wq->eventfd >= 0);
    priskv_set_fd_handler(wq->eventfd, priskv_workqueue_process, NULL, wq);
    priskv_add_event_fd(epollfd, wq->eventfd);

    pthread_spin_init(&wq->works_lock, 0);
    list_head_init(&wq->works);

    return wq;
}

void priskv_workqueue_destroy(priskv_workqueue *wq)
{
    if (!wq) {
        return;
    }

    priskv_workqueue_process(wq->eventfd, wq, EPOLLIN);

    close(wq->eventfd);
    free(wq);
}

static inline void priskv_work_init(priskv_work *work, int (*func)(void *arg), void *arg, bool sync)
{
    list_node_init(&work->node);
    work->func = func;
    work->arg = arg;
    work->sync = sync;
    work->retval = 0;
    work->done = false;

    if (sync) {
        pthread_cond_init(&work->wait, NULL);
        pthread_mutex_init(&work->mutex, NULL);
    }
}

int priskv_workqueue_call(priskv_workqueue *wq, int (*func)(void *arg), void *arg)
{
    priskv_work work;

    priskv_work_init(&work, func, arg, true);

    /* queue a work and try to kick target queue thread */
    pthread_spin_lock(&wq->works_lock);
    list_add_tail(&wq->works, &work.node);
    pthread_spin_unlock(&wq->works_lock);

    priskv_workqueue_kick(wq);

    pthread_mutex_lock(&work.mutex);
    while (!work.done) {
        pthread_cond_wait(&work.wait, &work.mutex);
    }
    pthread_mutex_unlock(&work.mutex);

    return work.retval;
}

void priskv_workqueue_submit(priskv_workqueue *wq, int (*func)(void *arg), void *arg)
{
    priskv_work *work = malloc(sizeof(priskv_work));

    priskv_work_init(work, func, arg, false);

    /* queue a work and try to kick target queue thread */
    pthread_spin_lock(&wq->works_lock);
    list_add_tail(&wq->works, &work->node);
    pthread_spin_unlock(&wq->works_lock);

    priskv_workqueue_kick(wq);
}
