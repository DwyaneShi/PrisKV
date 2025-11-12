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
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "priskv-utils.h"
#include "priskv-event.h"

/* event handling map is shared by all the threads */
#define PRISKV_FDS_GROW 64
static int priskv_fds;
static priskv_fd_handler *fd_handlers;
// static pthread_mutex_t fd_handlers_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_rwlock_t fd_handlers_mutex = PTHREAD_RWLOCK_INITIALIZER;

/* with lock held */
static void priskv_fd_handlers_try_grow(int fd)
{
    if (fd >= priskv_fds) {
        if (fd >= priskv_fds) {
            int newfds = ALIGN_UP(fd + 1, PRISKV_FDS_GROW);
            fd_handlers = reallocarray(fd_handlers, newfds, sizeof(priskv_fd_handler));
            priskv_fds = newfds;
        }
        assert(fd_handlers);
    }
}

void priskv_set_fd_handler(int fd, priskv_event_handler *pollin, priskv_event_handler *pollout,
                         void *opaque)
{
    priskv_fd_handler *handler;

    assert(fd >= 0);

    pthread_rwlock_wrlock(&fd_handlers_mutex);
    priskv_fd_handlers_try_grow(fd);

    handler = &fd_handlers[fd];
    handler->pollin = pollin;
    handler->pollout = pollout;
    handler->opaque = opaque;
    pthread_rwlock_unlock(&fd_handlers_mutex);
}

static inline void priskv_get_fd_handler(int fd, priskv_fd_handler *fd_handler)
{
    assert(fd < priskv_fds);
    pthread_rwlock_rdlock(&fd_handlers_mutex);
    memcpy(fd_handler, &fd_handlers[fd], sizeof(priskv_fd_handler));
    pthread_rwlock_unlock(&fd_handlers_mutex);
}

void priskv_fd_handler_event(struct epoll_event *event)
{
    priskv_fd_handler fd_handler;
    int fd = event->data.fd;

    priskv_get_fd_handler(fd, &fd_handler);

    if ((event->events & EPOLLIN) && fd_handler.pollin) {
        fd_handler.pollin(fd, fd_handler.opaque, EPOLLIN);
    }

    if ((event->events & EPOLLOUT) && fd_handler.pollout) {
        fd_handler.pollout(fd, fd_handler.opaque, EPOLLOUT);
    }
}

void priskv_events_process(int epollfd, int timeout)
{
    const int maxevents = 128;
    struct epoll_event events[maxevents];
    int nevents, i;

    nevents = epoll_wait(epollfd, events, maxevents, timeout);
    if (nevents <= 0) {
        return;
    }

    for (i = 0; i < nevents; i++) {
        struct epoll_event *event = &events[i];
        priskv_fd_handler_event(event);
    }
}
