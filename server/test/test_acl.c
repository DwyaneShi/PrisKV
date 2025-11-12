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

#include "priskv-log.h"
#include "acl.h"

static void test_acl4()
{
    const char *acl4_good_0 = "127.0.0.1";
    assert(!priskv_acl_add(acl4_good_0));
    assert(!__priskv_acl_verify("127.0.0.1"));

    const char *acl4_good_1 = "127.0.0.1/24";
    assert(!priskv_acl_add(acl4_good_1));
    assert(!__priskv_acl_verify("127.0.0.1"));
    assert(!__priskv_acl_verify("127.0.0.2"));
    assert(!__priskv_acl_verify("127.0.0.255"));
    assert(__priskv_acl_verify("127.0.1.255"));
    assert(__priskv_acl_verify("128.0.0.1"));

    assert(!priskv_acl_del(acl4_good_0));
    assert(!priskv_acl_del(acl4_good_1));
    assert(__priskv_acl_verify("127.0.0.1"));
    assert(__priskv_acl_verify("127.0.0.2"));
    assert(__priskv_acl_verify("127.0.0.255"));

    const char *acl4_bad_0 = "257.0.0.1";
    assert(priskv_acl_add(acl4_bad_0));

    const char *acl4_bad_1 = "127.0.0.1/36";
    assert(priskv_acl_add(acl4_bad_1));

    const char *acl_any = "ANY";
    assert(!priskv_acl_add(acl_any));
    assert(!__priskv_acl_verify("127.0.0.1"));
    assert(!__priskv_acl_verify("127.0.0.2"));
    assert(!__priskv_acl_verify("127.0.0.255"));
    assert(!__priskv_acl_verify("192.0.0.1"));
    assert(!__priskv_acl_verify("192.0.0.2"));
    assert(!__priskv_acl_verify("192.0.0.255"));

    assert(!priskv_acl_del(acl_any));
}

static void test_acl6()
{
    const char *acl6_good_0 = "fdbd:ff1:ce00:4c7:98ce:62c:f3fc:1247";
    assert(!priskv_acl_add(acl6_good_0));
    assert(!__priskv_acl_verify("fdbd:ff1:ce00:4c7:98ce:62c:f3fc:1247"));
    assert(__priskv_acl_verify("fdbd:ff1:ce00:4c7:98ce:62c:f3fc:1248"));

    const char *acl6_good_1 = "fdbd:ff1:ce00:4c7:98ce:62c:f3fc:1247/72";
    assert(!priskv_acl_add(acl6_good_1));
    assert(!__priskv_acl_verify("fdbd:ff1:ce00:4c7:98ce:62c:f3fc:1247"));
    assert(!__priskv_acl_verify("fdbd:ff1:ce00:4c7:98ce:62c:f3fc:1248"));
    assert(!__priskv_acl_verify("fdbd:ff1:ce00:4c7:98ce:62c:e3fc:1248"));
    assert(!__priskv_acl_verify("fdbd:ff1:ce00:4c7:98ce:72c:e3fc:1248"));
    assert(__priskv_acl_verify("fdbd:ff1:ce00:4c7:a8ce:72c:e3fc:1248"));

    assert(!priskv_acl_del(acl6_good_0));
    assert(!priskv_acl_del(acl6_good_1));
    assert(__priskv_acl_verify("fdbd:ff1:ce00:4c7:98ce:62c:f3fc:1248"));
    assert(__priskv_acl_verify("fdbd:ff1:ce00:4c7:a8ce:72c:e3fc:1248"));

    const char *acl6_bad_0 = "fdbd:ff1:ce00:4c7:98ce:62c:f3fc:1247/130";
    assert(priskv_acl_add(acl6_bad_0));
    const char *acl6_bad_1 = "fdbd:ff1:ce00:4c7:98ce:62c:f3fc:12479/72";
    assert(priskv_acl_add(acl6_bad_1));

    const char *acl_any = "ANY";
    assert(!priskv_acl_add(acl_any));
    assert(!__priskv_acl_verify("fdbd:ff1:ce00:4c7:98ce:62c:f3fc:1247"));
    assert(!__priskv_acl_verify("fdbd:ff1:ce00:4c7:98ce:62c:f3fc:1248"));
    assert(!__priskv_acl_verify("fdbd:ff1:ce00:4c7:98ce:62c:e3fc:1248"));
    assert(!__priskv_acl_verify("fdbd:ff1:ce00:4c7:98ce:72c:e3fc:1248"));

    assert(!priskv_acl_del(acl_any));
}

int main()
{
    priskv_set_log_level(priskv_log_info);

    test_acl4();
    test_acl6();

    return 0;
}
