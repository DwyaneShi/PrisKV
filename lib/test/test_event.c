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

#include "priskv-event.h"

#define PRISKV_EVNET_TEST_FDS 10000
static int totalfds = PRISKV_EVNET_TEST_FDS;
static int setfd;
static int *fds;

static pthread_spinlock_t lock;

static void priskv_test_event_handler(int fd, void *opaque, uint32_t events)
{
    int *expt = opaque;

    assert(fd == *expt);
}

static void *test_set_event(void *arg)
{
    int fd;

    while (1) {
        pthread_spin_lock(&lock);
        if (setfd == totalfds) {
            pthread_spin_unlock(&lock);
            break;
        }

        fd = setfd;
        setfd++;
        pthread_spin_unlock(&lock);

        priskv_set_fd_handler(fd, priskv_test_event_handler, NULL, &fds[fd]);
    }

    return NULL;
}

static void *test_handle_event(void *arg)
{
    int fd;

    while (1) {
        pthread_spin_lock(&lock);
        if (setfd == totalfds) {
            pthread_spin_unlock(&lock);
            break;
        }

        fd = setfd;
        setfd++;
        pthread_spin_unlock(&lock);

        struct epoll_event event = {.data.fd = fd, .events = EPOLLIN};

        priskv_fd_handler_event(&event);
    }

    return NULL;
}

int main()
{
    int threads = 4;
    pthread_t thread[threads];

    pthread_spin_init(&lock, 0);

    fds = calloc(sizeof(int), totalfds);
    assert(fds);
    for (int i = 0; i < totalfds; i++) {
        fds[i] = i;
    }

    for (int i = 0; i < threads; i++) {
        assert(!pthread_create(&thread[i], NULL, test_set_event, NULL));
    }

    for (int i = 0; i < threads; i++) {
        assert(!pthread_join(thread[i], NULL));
    }

    assert(setfd == totalfds);

    setfd = 0;
    for (int i = 0; i < threads; i++) {
        assert(!pthread_create(&thread[i], NULL, test_handle_event, NULL));
    }

    for (int i = 0; i < threads; i++) {
        assert(!pthread_join(thread[i], NULL));
    }

    return 0;
}
