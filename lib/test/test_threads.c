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
#include <sys/timerfd.h>

#include "priskv-threads.h"
#include "priskv-event.h"
#include "priskv-utils.h"
#include "priskv-log.h"

/* in second */
#define TEST_DURATION 2

/* in nanosencond */
#define TEST_INTERVAL (1000 * 1000 * 10)

typedef struct test_timer {
    struct itimerspec spec;
    int counter;
} test_timer;

static void test_timer_pollin(int fd, void *opaque, uint32_t events)
{
    test_timer *t = opaque;
    uint64_t exp;

    read(fd, &exp, sizeof(uint64_t));
    t->counter++;
    printf("Timer HIT: %d\n", t->counter);
}

int main()
{
    // priskv_set_log_level(priskv_log_debug);
    uint8_t iothreads = 2, bgthreads = 2;
    priskv_threadpool *pool = priskv_threadpool_create("test", iothreads, bgthreads, 0);
    assert(pool);
    usleep(100); /* wait threads ready */

    priskv_thread *bgthread = priskv_threadpool_find_bgthread(pool);
    assert(bgthread);

    int timerfd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK);
    assert(timerfd > 0);

    struct timespec start;
    assert(!clock_gettime(CLOCK_REALTIME, &start));

    test_timer timer = {0};
    timer.spec.it_value.tv_sec = start.tv_sec;
    timer.spec.it_value.tv_nsec = start.tv_nsec;
    timer.spec.it_interval.tv_sec = 0;
    timer.spec.it_interval.tv_nsec = TEST_INTERVAL;
    timer.counter = 0;
    assert(!timerfd_settime(timerfd, TFD_TIMER_ABSTIME, &timer.spec, NULL));

    priskv_set_fd_handler(timerfd, test_timer_pollin, NULL, &timer);
    priskv_thread_add_event_handler(bgthread, timerfd);

    struct timespec end;
    while (1) {
        assert(!clock_gettime(CLOCK_REALTIME, &end));
        if (end.tv_sec - start.tv_sec < TEST_DURATION) {
            sleep(1);
        } else {
            break;
        }
    }

    assert(timer.counter > (TEST_DURATION * 1000 * 1000 * 1000 / TEST_INTERVAL));

    priskv_threadpool_destroy(pool);

    return 0;
}
