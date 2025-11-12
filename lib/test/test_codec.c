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
#include <sys/time.h>
#include <string.h>

#include "priskv-log.h"
#include "priskv-codec.h"

static double get_clock_ns()
{
    int res;
    double ns;
    struct timeval tv;

    res = gettimeofday(&tv, NULL);
    ns = (double)tv.tv_sec * 1E9 + (double)tv.tv_usec * 1000;
    if (res == -1) {
        fprintf(stderr, "could not get requested clock\n");
        exit(10);
    }

    return ns;
}

static double diff_s(double start, double end)
{
    return (end - start) / 1E9;
}

void strtrim(char *s, char c)
{
    char *p;
    for (p = s; *p != '\0'; p++) {
        if (*p != c) {
            *s++ = *p;
        }
    }
    *s = '\0';
}

typedef struct priskv_unittest_t {
    const char *name;
    void (*test)(void);
} priskv_unittest_t;

#define priskv_unittest(f) {#f, f}

static void priskv_unittest_run(priskv_unittest_t *tests, int ntest)
{
    double total_runtime = 0;

    for (int i = 0; i < ntest; i++) {
        printf("\n┌─ TEST[%d] [RUN] %s\n", i, tests[i].name);
        double start = get_clock_ns();
        tests[i].test();
        double end = get_clock_ns();
        double runtime = diff_s(start, end);
        total_runtime += runtime;
        printf("└─ TEST[%d] [OK]  %s, costs %.6f seconds\n", i, tests[i].name, runtime);
    }

    printf("-----------ALL TESTS [OK], costs %.6f seconds-----------\n", total_runtime);
}

#define priskv_unittest_run_all(tests) priskv_unittest_run(tests, sizeof(tests) / sizeof(tests[0]))

typedef struct test_string_array_t {
    int test;
    const char **strs;
    int nstr;
} test_string_array_t;

PRISKV_DECL_OBJECT_BEGIN(test_string_array_t)
PRISKV_DECL_OBJECT_VALUE_FIELD(test_string_array_t, "test", test, priskv_int, required, ignored)
PRISKV_DECL_OBJECT_ARRAY_FIELD(test_string_array_t, "strs", strs, nstr, priskv_string, required,
                             ignored)
PRISKV_DECL_OBJECT_END(test_string_array_t, test_string_array_t)

static void test_codec_new_and_destroy()
{
    priskv_codec *codec = priskv_codec_new();
    assert(codec);
    priskv_codec_destroy(codec);
}

struct test_int {
    int int_val_required;
    int int_val_optional;
};

PRISKV_DECL_OBJECT_BEGIN(test_int)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_int, "int_val_required", int_val_required, priskv_int,
                             required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_int, "int_val_optional", int_val_optional, priskv_int,
                             optional, forced)
PRISKV_DECL_OBJECT_END(test_int, struct test_int)

PRISKV_DECL_OBJECT_BEGIN(test_int_ignored)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_int, "int_val_required", int_val_required, priskv_int,
                             required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_int, "int_val_optional", int_val_optional, priskv_int,
                             optional, ignored)
PRISKV_DECL_OBJECT_END(test_int_ignored, struct test_int)

static void test_codec_decode_and_code_int()
{
    priskv_codec *codec = priskv_codec_new();
    char test_int_json[] = "{ \"int_val_required\": 101010101, \"int_val_optional\": 202020202 }";
    struct test_int test_int = {.int_val_required = 101010101, .int_val_optional = 202020202};
    struct test_int *test_int_tmp = priskv_codec_decode(codec, test_int_json, &test_int_obj);

    assert(test_int_tmp != NULL);
    assert(test_int_tmp->int_val_required == test_int.int_val_required);
    assert(test_int_tmp->int_val_optional == test_int.int_val_optional);

    assert(!priskv_codec_free_struct(codec, test_int_tmp, &test_int_obj));

    char *str = priskv_codec_code(codec, &test_int, &test_int_obj);
    assert(!strcmp(str, test_int_json));
    free(str);

    priskv_codec_destroy(codec);
}

static void test_codec_decode_not_int()
{
    priskv_codec *codec = priskv_codec_new();
    char test_int_json[] = "{ \"int_val_required\": true }";
    struct test_int *test_int_tmp = priskv_codec_decode(codec, test_int_json, &test_int_obj);
    assert(test_int_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "failed to decode `int_val_required`: type is not int"));
    priskv_codec_destroy(codec);
}

static void test_codec_decode_int_optional()
{
    priskv_codec *codec = priskv_codec_new();
    char test_int_json[] = "{ \"int_val_required\": 101010101 }";
    struct test_int *test_int_tmp = priskv_codec_decode(codec, test_int_json, &test_int_obj);

    assert(test_int_tmp != NULL);
    assert(test_int_tmp->int_val_required == 101010101);
    assert(test_int_tmp->int_val_optional == 0);

    assert(!priskv_codec_free_struct(codec, test_int_tmp, &test_int_obj));

    priskv_codec_destroy(codec);
}

static void test_codec_decode_int_missing_required()
{
    priskv_codec *codec = priskv_codec_new();
    char test_int_json[] = "{ \"int_val_optional\": 202020202 }";
    struct test_int *test_int_tmp = priskv_codec_decode(codec, test_int_json, &test_int_obj);

    assert(test_int_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec), "not found `int_val_required` that is required"));

    priskv_codec_destroy(codec);
}

static void test_codec_code_int_ignored()
{
    priskv_codec *codec = priskv_codec_new();
    struct test_int test_int_null = {.int_val_required = 0, .int_val_optional = 0};

    char *str = priskv_codec_code(codec, &test_int_null, &test_int_ignored_obj);
    assert(!strcmp(str, "{ \"int_val_required\": 0 }"));
    free(str);

    priskv_codec_destroy(codec);
}

struct test_uint64_t {
    uint64_t uint64_t_val_required;
    uint64_t uint64_t_val_optional;
};

PRISKV_DECL_OBJECT_BEGIN(test_uint64_t)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_uint64_t, "uint64_t_val_required", uint64_t_val_required,
                             priskv_uint64, required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_uint64_t, "uint64_t_val_optional", uint64_t_val_optional,
                             priskv_uint64, optional, forced)
PRISKV_DECL_OBJECT_END(test_uint64_t, struct test_uint64_t)

PRISKV_DECL_OBJECT_BEGIN(test_uint64_t_ignored)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_uint64_t, "uint64_t_val_required", uint64_t_val_required,
                             priskv_uint64, required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_uint64_t, "uint64_t_val_optional", uint64_t_val_optional,
                             priskv_uint64, optional, ignored)
PRISKV_DECL_OBJECT_END(test_uint64_t_ignored, struct test_uint64_t)

static void test_codec_decode_and_code_uint64_t()
{
    priskv_codec *codec = priskv_codec_new();
    char test_uint64_t_json[] = "{ \"uint64_t_val_required\": 18446744073709551615, "
                                "\"uint64_t_val_optional\": 18446744073709551615 }";
    struct test_uint64_t test_uint64_t = {.uint64_t_val_required = 18446744073709551615ULL,
                                          .uint64_t_val_optional = 18446744073709551615ULL};
    struct test_uint64_t *test_uint64_t_tmp =
        priskv_codec_decode(codec, test_uint64_t_json, &test_uint64_t_obj);

    assert(test_uint64_t_tmp != NULL);
    assert(test_uint64_t_tmp->uint64_t_val_required == test_uint64_t.uint64_t_val_required);
    assert(test_uint64_t_tmp->uint64_t_val_optional == test_uint64_t.uint64_t_val_optional);

    assert(!priskv_codec_free_struct(codec, test_uint64_t_tmp, &test_uint64_t_obj));

    char *str = priskv_codec_code(codec, &test_uint64_t, &test_uint64_t_obj);
    assert(!strcmp(str, test_uint64_t_json));
    free(str);

    priskv_codec_destroy(codec);
}

static void test_codec_decode_not_uint64_t()
{
    priskv_codec *codec = priskv_codec_new();
    char test_uint64_t_json[] = "{ \"uint64_t_val_required\": true }";
    struct test_uint64_t *test_uint64_t_tmp =
        priskv_codec_decode(codec, test_uint64_t_json, &test_uint64_t_obj);
    assert(test_uint64_t_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "failed to decode `uint64_t_val_required`: type is not int"));
    priskv_codec_destroy(codec);
}

static void test_codec_decode_uint64_t_optional()
{
    priskv_codec *codec = priskv_codec_new();
    char test_uint64_t_json[] = "{ \"uint64_t_val_required\": 18446744073709551615 }";

    struct test_uint64_t *test_uint64_t_tmp =
        priskv_codec_decode(codec, test_uint64_t_json, &test_uint64_t_obj);

    assert(test_uint64_t_tmp != NULL);
    assert(test_uint64_t_tmp->uint64_t_val_required == 18446744073709551615ULL);
    assert(test_uint64_t_tmp->uint64_t_val_optional == 0);

    assert(!priskv_codec_free_struct(codec, test_uint64_t_tmp, &test_uint64_t_obj));

    priskv_codec_destroy(codec);
}

static void test_codec_decode_uint64_t_missing_required()
{
    priskv_codec *codec = priskv_codec_new();
    char test_uint64_t_json[] = "{ \"uint64_t_val_optional\": 18446744073709551615 }";

    struct test_uint64_t *test_uint64_t_tmp =
        priskv_codec_decode(codec, test_uint64_t_json, &test_uint64_t_obj);

    assert(test_uint64_t_tmp == NULL);
    assert(
        !strcmp(priskv_codec_get_error(codec), "not found `uint64_t_val_required` that is required"));

    priskv_codec_destroy(codec);
}

static void test_codec_code_uint64_t_ignored()
{
    priskv_codec *codec = priskv_codec_new();
    struct test_uint64_t test_uint64_t_null = {.uint64_t_val_required = 0,
                                               .uint64_t_val_optional = 0};

    char *str = priskv_codec_code(codec, &test_uint64_t_null, &test_uint64_t_ignored_obj);
    assert(!strcmp(str, "{ \"uint64_t_val_required\": 0 }"));
    free(str);

    priskv_codec_destroy(codec);
}

struct test_boolean {
    bool boolean_val_required;
    bool boolean_val_optional;
};

PRISKV_DECL_OBJECT_BEGIN(test_boolean)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_boolean, "boolean_val_required", boolean_val_required,
                             priskv_boolean, required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_boolean, "boolean_val_optional", boolean_val_optional,
                             priskv_boolean, optional, forced)
PRISKV_DECL_OBJECT_END(test_boolean, struct test_boolean)

PRISKV_DECL_OBJECT_BEGIN(test_boolean_ignored)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_boolean, "boolean_val_required", boolean_val_required,
                             priskv_boolean, required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_boolean, "boolean_val_optional", boolean_val_optional,
                             priskv_boolean, optional, ignored)
PRISKV_DECL_OBJECT_END(test_boolean_ignored, struct test_boolean)

static void test_codec_decode_and_code_boolean()
{
    priskv_codec *codec = priskv_codec_new();
    char test_boolean_json[] =
        "{ \"boolean_val_required\": true, \"boolean_val_optional\": false }";
    struct test_boolean test_boolean = {.boolean_val_required = true,
                                        .boolean_val_optional = false};
    struct test_boolean *test_boolean_tmp =
        priskv_codec_decode(codec, test_boolean_json, &test_boolean_obj);

    assert(test_boolean_tmp != NULL);
    assert(test_boolean_tmp->boolean_val_required == test_boolean.boolean_val_required);
    assert(test_boolean_tmp->boolean_val_optional == test_boolean.boolean_val_optional);

    assert(!priskv_codec_free_struct(codec, test_boolean_tmp, &test_boolean_obj));

    char *str = priskv_codec_code(codec, &test_boolean, &test_boolean_obj);
    assert(!strcmp(str, test_boolean_json));
    free(str);

    priskv_codec_destroy(codec);
}

static void test_codec_decode_not_boolean()
{
    priskv_codec *codec = priskv_codec_new();
    char test_boolean_json[] = "{ \"boolean_val_required\": \"foo\" }";
    struct test_boolean *test_boolean_tmp =
        priskv_codec_decode(codec, test_boolean_json, &test_boolean_obj);
    assert(test_boolean_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "failed to decode `boolean_val_required`: type is not boolean"));
    priskv_codec_destroy(codec);
}

static void test_codec_decode_boolean_optional()
{
    priskv_codec *codec = priskv_codec_new();
    char test_boolean_json[] = "{ \"boolean_val_required\": true }";

    struct test_boolean *test_boolean_tmp =
        priskv_codec_decode(codec, test_boolean_json, &test_boolean_obj);

    assert(test_boolean_tmp != NULL);
    assert(test_boolean_tmp->boolean_val_required == true);
    assert(test_boolean_tmp->boolean_val_optional == false);

    assert(!priskv_codec_free_struct(codec, test_boolean_tmp, &test_boolean_obj));

    priskv_codec_destroy(codec);
}

static void test_codec_decode_boolean_missing_required()
{
    priskv_codec *codec = priskv_codec_new();
    char test_boolean_json[] = "{ \"boolean_val_optional\": false }";

    struct test_boolean *test_boolean_tmp =
        priskv_codec_decode(codec, test_boolean_json, &test_boolean_obj);

    assert(test_boolean_tmp == NULL);
    assert(
        !strcmp(priskv_codec_get_error(codec), "not found `boolean_val_required` that is required"));

    priskv_codec_destroy(codec);
}

static void test_codec_code_boolean_ignored()
{
    priskv_codec *codec = priskv_codec_new();
    struct test_boolean test_boolean_null = {.boolean_val_required = false,
                                             .boolean_val_optional = false};

    char *str = priskv_codec_code(codec, &test_boolean_null, &test_boolean_ignored_obj);
    assert(!strcmp(str, "{ \"boolean_val_required\": false, \"boolean_val_optional\": false }"));
    free(str);

    priskv_codec_destroy(codec);
}

struct test_string {
    const char *string_val_required;
    const char *string_val_optional;
};

PRISKV_DECL_OBJECT_BEGIN(test_string)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_string, "string_val_required", string_val_required,
                             priskv_string, required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_string, "string_val_optional", string_val_optional,
                             priskv_string, optional, forced)
PRISKV_DECL_OBJECT_END(test_string, struct test_string)

PRISKV_DECL_OBJECT_BEGIN(test_string_ignored)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_string, "string_val_required", string_val_required,
                             priskv_string, required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_string, "string_val_optional", string_val_optional,
                             priskv_string, optional, ignored)
PRISKV_DECL_OBJECT_END(test_string_ignored, struct test_string)

static void test_codec_decode_and_code_string()
{
    priskv_codec *codec = priskv_codec_new();
    char test_string_json[] =
        "{ \"string_val_required\": \"string1\", \"string_val_optional\": \"string2\" }";
    struct test_string test_string = {.string_val_required = "string1",
                                      .string_val_optional = "string2"};
    struct test_string *test_string_tmp =
        priskv_codec_decode(codec, test_string_json, &test_string_obj);

    assert(test_string_tmp != NULL);
    assert(!strcmp(test_string_tmp->string_val_required, test_string.string_val_required));
    assert(!strcmp(test_string_tmp->string_val_optional, test_string.string_val_optional));

    assert(!priskv_codec_free_struct(codec, test_string_tmp, &test_string_obj));

    char *str = priskv_codec_code(codec, &test_string, &test_string_obj);
    assert(!strcmp(str, test_string_json));
    free(str);

    priskv_codec_destroy(codec);
}

static void test_codec_decode_not_string()
{
    priskv_codec *codec = priskv_codec_new();
    char test_string_json[] = "{ \"string_val_required\": true }";
    struct test_string *test_string_tmp =
        priskv_codec_decode(codec, test_string_json, &test_string_obj);
    assert(test_string_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "failed to decode `string_val_required`: type is not string"));
    priskv_codec_destroy(codec);
}

static void test_codec_decode_string_optional()
{
    priskv_codec *codec = priskv_codec_new();
    char test_string_json[] = "{ \"string_val_required\": \"string1\" }";

    struct test_string *test_string_tmp =
        priskv_codec_decode(codec, test_string_json, &test_string_obj);

    assert(test_string_tmp != NULL);
    assert(!strcmp(test_string_tmp->string_val_required, "string1"));
    assert(test_string_tmp->string_val_optional == NULL);

    assert(!priskv_codec_free_struct(codec, test_string_tmp, &test_string_obj));

    priskv_codec_destroy(codec);
}

static void test_codec_decode_string_missing_required()
{
    priskv_codec *codec = priskv_codec_new();
    char test_string_json[] = "{ \"string_val_optional\": \"string2\" }";

    struct test_string *test_string_tmp =
        priskv_codec_decode(codec, test_string_json, &test_string_obj);

    assert(test_string_tmp == NULL);
    assert(
        !strcmp(priskv_codec_get_error(codec), "not found `string_val_required` that is required"));

    priskv_codec_destroy(codec);
}

static void test_codec_code_string_ignored()
{
    priskv_codec *codec = priskv_codec_new();
    struct test_string test_string_null = {.string_val_required = NULL,
                                           .string_val_optional = NULL};

    char *str = priskv_codec_code(codec, &test_string_null, &test_string_ignored_obj);
    assert(!strcmp(str, "{ \"string_val_required\": \"\" }"));
    free(str);

    priskv_codec_destroy(codec);
}

struct test_struct_child {
    int val;
};

struct test_struct_parent {
    int val;
    struct test_struct_child child_required;
    struct test_struct_child child_optional;
};

PRISKV_DECL_OBJECT_BEGIN(test_struct_child)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_struct_child, "val", val, priskv_int, required, forced)
PRISKV_DECL_OBJECT_END(test_struct_child, struct test_struct_child)

PRISKV_DECL_OBJECT_BEGIN(test_struct_parent)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_struct_parent, "val", val, priskv_int, required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_struct_parent, "child_required", child_required,
                             test_struct_child, required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct test_struct_parent, "child_optional", child_optional,
                             test_struct_child, optional, forced)
PRISKV_DECL_OBJECT_END(test_struct_parent, struct test_struct_parent)

static void test_codec_decode_and_code_struct()
{
    priskv_codec *codec = priskv_codec_new();
    char test_struct_json[] = "{ \"val\": 111, \"child_required\": { \"val\": 222 }, "
                              "\"child_optional\": { \"val\": 333 } }";
    struct test_struct_parent test_struct = {.val = 111,
                                             .child_required = {.val = 222},
                                             .child_optional = {.val = 333}};
    struct test_struct_parent *test_struct_tmp =
        priskv_codec_decode(codec, test_struct_json, &test_struct_parent_obj);

    assert(test_struct_tmp != NULL);
    assert(test_struct_tmp->val == test_struct.val);
    assert(test_struct_tmp->child_required.val == test_struct.child_required.val);
    assert(test_struct_tmp->child_optional.val == test_struct.child_optional.val);

    assert(!priskv_codec_free_struct(codec, test_struct_tmp, &test_struct_parent_obj));

    char *str = priskv_codec_code(codec, &test_struct, &test_struct_parent_obj);
    assert(!strcmp(str, test_struct_json));
    free(str);

    priskv_codec_destroy(codec);
}

static void test_codec_decode_not_struct()
{
    priskv_codec *codec = priskv_codec_new();
    char test_struct_json[] =
        "{ \"val\": 111, \"child_required\": true, \"child_optional\": true }";
    struct test_struct_parent *test_struct_tmp =
        priskv_codec_decode(codec, test_struct_json, &test_struct_parent_obj);

    assert(test_struct_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "failed to decode `child_required`: type is not object"));
    priskv_codec_destroy(codec);
}

static void test_codec_decode_struct_optional()
{
    priskv_codec *codec = priskv_codec_new();
    char test_struct_json[] = "{ \"val\": 111, \"child_required\": { \"val\": 222 } }";
    struct test_struct_parent *test_struct_tmp =
        priskv_codec_decode(codec, test_struct_json, &test_struct_parent_obj);

    assert(test_struct_tmp != NULL);
    assert(test_struct_tmp->val == 111);
    assert(test_struct_tmp->child_required.val == 222);
    assert(test_struct_tmp->child_optional.val == 0);

    assert(!priskv_codec_free_struct(codec, test_struct_tmp, &test_struct_parent_obj));

    priskv_codec_destroy(codec);
}

static void test_codec_decode_struct_missing_required()
{
    priskv_codec *codec = priskv_codec_new();
    char test_struct_json[] = "{ \"val\": 111, \"child_optional\": { \"val\": 333 } }";
    struct test_struct_parent *test_struct_tmp =
        priskv_codec_decode(codec, test_struct_json, &test_struct_parent_obj);

    assert(test_struct_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec), "not found `child_required` that is required"));

    priskv_codec_destroy(codec);
}

struct test_int_array {
    int *int_vals_required;
    int nint_vals_required;
    int *int_vals_optional;
    int nint_vals_optional;
};

PRISKV_DECL_OBJECT_BEGIN(test_int_array)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_int_array, "int_vals_required", int_vals_required,
                             nint_vals_required, priskv_int, required, forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_int_array, "int_vals_optional", int_vals_optional,
                             nint_vals_optional, priskv_int, optional, forced)
PRISKV_DECL_OBJECT_END(test_int_array, struct test_int_array)

PRISKV_DECL_OBJECT_BEGIN(test_int_array_ignored)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_int_array, "int_vals_required", int_vals_required,
                             nint_vals_required, priskv_int, required, forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_int_array, "int_vals_optional", int_vals_optional,
                             nint_vals_optional, priskv_int, optional, ignored)
PRISKV_DECL_OBJECT_END(test_int_array_ignored, struct test_int_array)

static void test_codec_decode_and_code_int_array()
{
    priskv_codec *codec = priskv_codec_new();
    char test_int_array_json[] =
        "{ \"int_vals_required\": [ 1, 2, 3 ], \"int_vals_optional\": [ 4, 5 ] }";
    int test_ints_val_required[] = {1, 2, 3};
    int test_ints_val_optional[] = {4, 5};
    struct test_int_array test_int_array = {.int_vals_required = test_ints_val_required,
                                            .nint_vals_required = 3,
                                            .int_vals_optional = test_ints_val_optional,
                                            .nint_vals_optional = 2};
    struct test_int_array *test_int_array_tmp =
        priskv_codec_decode(codec, test_int_array_json, &test_int_array_obj);

    assert(test_int_array_tmp != NULL);
    assert(test_int_array_tmp->nint_vals_required == test_int_array.nint_vals_required);
    for (int i = 0; i < test_int_array.nint_vals_required; i++) {
        assert(test_int_array_tmp->int_vals_required[i] == test_int_array.int_vals_required[i]);
    }

    assert(test_int_array_tmp->nint_vals_optional == test_int_array.nint_vals_optional);
    for (int i = 0; i < test_int_array.nint_vals_optional; i++) {
        assert(test_int_array_tmp->int_vals_optional[i] == test_int_array.int_vals_optional[i]);
    }

    assert(!priskv_codec_free_struct(codec, test_int_array_tmp, &test_int_array_obj));

    char *str = priskv_codec_code(codec, &test_int_array, &test_int_array_obj);
    assert(!strcmp(str, test_int_array_json));
    free(str);

    priskv_codec_destroy(codec);
}

static void test_codec_decode_int_not_array()
{
    priskv_codec *codec = priskv_codec_new();
    char test_int_array_json[] = "{ \"int_vals_required\": 1 }";
    struct test_int_array *test_int_array_tmp =
        priskv_codec_decode(codec, test_int_array_json, &test_int_array_obj);

    assert(test_int_array_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "failed to decode `int_vals_required`: type is not array"));
    priskv_codec_destroy(codec);
}

static void test_codec_decode_not_int_array()
{
    priskv_codec *codec = priskv_codec_new();
    char test_int_array_json[] = "{ \"int_vals_required\": [true, false] }";
    struct test_int_array *test_int_array_tmp =
        priskv_codec_decode(codec, test_int_array_json, &test_int_array_obj);

    assert(test_int_array_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "failed to decode array `int_vals_required`: type is not int"));
    priskv_codec_destroy(codec);
}

static void test_codec_decode_int_array_optional()
{
    priskv_codec *codec = priskv_codec_new();
    char test_int_array_json[] = "{ \"int_vals_required\": [ 1, 2, 3 ] }";
    int test_ints_val_required[] = {1, 2, 3};
    struct test_int_array test_int_array = {.int_vals_required = test_ints_val_required,
                                            .nint_vals_required = 3};
    struct test_int_array *test_int_array_tmp =
        priskv_codec_decode(codec, test_int_array_json, &test_int_array_obj);

    assert(test_int_array_tmp != NULL);
    assert(test_int_array_tmp->nint_vals_required == test_int_array.nint_vals_required);
    for (int i = 0; i < test_int_array.nint_vals_required; i++) {
        assert(test_int_array_tmp->int_vals_required[i] == test_int_array.int_vals_required[i]);
    }

    assert(test_int_array_tmp->nint_vals_optional == 0);
    assert(test_int_array_tmp->int_vals_optional == NULL);

    assert(!priskv_codec_free_struct(codec, test_int_array_tmp, &test_int_array_obj));

    priskv_codec_destroy(codec);
}

static void test_codec_decode_int_array_missing_required()
{
    priskv_codec *codec = priskv_codec_new();
    char test_int_array_json[] = "{ \"int_vals_optional\": [ 4, 5 ] }";
    struct test_int_array *test_int_array_tmp =
        priskv_codec_decode(codec, test_int_array_json, &test_int_array_obj);

    assert(test_int_array_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec), "not found `int_vals_required` that is required"));

    priskv_codec_destroy(codec);
}

static void test_codec_code_int_array_ignored()
{
    priskv_codec *codec = priskv_codec_new();
    int test_ints_val_required[] = {1, 2, 3};
    struct test_int_array test_int_array = {.int_vals_required = test_ints_val_required,
                                            .nint_vals_required = 3,
                                            .int_vals_optional = NULL,
                                            .nint_vals_optional = 0};

    char *str = priskv_codec_code(codec, &test_int_array, &test_int_array_ignored_obj);
    assert(!strcmp(str, "{ \"int_vals_required\": [ 1, 2, 3 ] }"));
    free(str);

    priskv_codec_destroy(codec);
}

static void test_codec_code_int_array_forced()
{
    priskv_codec *codec = priskv_codec_new();
    struct test_int_array test_int_array = {.int_vals_required = NULL,
                                            .nint_vals_required = 0,
                                            .int_vals_optional = NULL,
                                            .nint_vals_optional = 0};

    char *str = priskv_codec_code(codec, &test_int_array, &test_int_array_ignored_obj);
    assert(!strcmp(str, "{ \"int_vals_required\": [ ] }"));
    free(str);

    priskv_codec_destroy(codec);
}

struct test_uint64_t_array {
    uint64_t *uint64_t_vals_required;
    int nuint64_t_vals_required;
    uint64_t *uint64_t_vals_optional;
    int nuint64_t_vals_optional;
};

PRISKV_DECL_OBJECT_BEGIN(test_uint64_t_array)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_uint64_t_array, "uint64_t_vals_required",
                             uint64_t_vals_required, nuint64_t_vals_required, priskv_uint64, required,
                             forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_uint64_t_array, "uint64_t_vals_optional",
                             uint64_t_vals_optional, nuint64_t_vals_optional, priskv_uint64, optional,
                             forced)
PRISKV_DECL_OBJECT_END(test_uint64_t_array, struct test_uint64_t_array)

PRISKV_DECL_OBJECT_BEGIN(test_uint64_t_array_ignored)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_uint64_t_array, "uint64_t_vals_required",
                             uint64_t_vals_required, nuint64_t_vals_required, priskv_uint64, required,
                             forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_uint64_t_array, "uint64_t_vals_optional",
                             uint64_t_vals_optional, nuint64_t_vals_optional, priskv_uint64, optional,
                             ignored)
PRISKV_DECL_OBJECT_END(test_uint64_t_array_ignored, struct test_uint64_t_array)

static void test_codec_decode_and_code_uint64_t_array()
{
    priskv_codec *codec = priskv_codec_new();
    char test_uint64_t_array_json[] =
        "{ \"uint64_t_vals_required\": [ 1, 2, 3 ], \"uint64_t_vals_optional\": [ 4, 5 ] }";
    uint64_t test_uint64_ts_val_required[] = {1, 2, 3};
    uint64_t test_uint64_ts_val_optional[] = {4, 5};
    struct test_uint64_t_array test_uint64_t_array = {
        .uint64_t_vals_required = test_uint64_ts_val_required,
        .nuint64_t_vals_required = 3,
        .uint64_t_vals_optional = test_uint64_ts_val_optional,
        .nuint64_t_vals_optional = 2};
    struct test_uint64_t_array *test_uint64_t_array_tmp =
        priskv_codec_decode(codec, test_uint64_t_array_json, &test_uint64_t_array_obj);

    assert(test_uint64_t_array_tmp != NULL);
    assert(test_uint64_t_array_tmp->nuint64_t_vals_required ==
           test_uint64_t_array.nuint64_t_vals_required);
    for (int i = 0; i < test_uint64_t_array.nuint64_t_vals_required; i++) {
        assert(test_uint64_t_array_tmp->uint64_t_vals_required[i] ==
               test_uint64_t_array.uint64_t_vals_required[i]);
    }

    assert(test_uint64_t_array_tmp->nuint64_t_vals_optional ==
           test_uint64_t_array.nuint64_t_vals_optional);
    for (int i = 0; i < test_uint64_t_array.nuint64_t_vals_optional; i++) {
        assert(test_uint64_t_array_tmp->uint64_t_vals_optional[i] ==
               test_uint64_t_array.uint64_t_vals_optional[i]);
    }

    assert(!priskv_codec_free_struct(codec, test_uint64_t_array_tmp, &test_uint64_t_array_obj));

    char *str = priskv_codec_code(codec, &test_uint64_t_array, &test_uint64_t_array_obj);
    assert(!strcmp(str, test_uint64_t_array_json));
    free(str);

    priskv_codec_destroy(codec);
}

static void test_codec_decode_uint64_t_not_array()
{
    priskv_codec *codec = priskv_codec_new();
    char test_uint64_t_array_json[] = "{ \"uint64_t_vals_required\": 1 }";
    struct test_uint64_t_array *test_uint64_t_array_tmp =
        priskv_codec_decode(codec, test_uint64_t_array_json, &test_uint64_t_array_obj);

    assert(test_uint64_t_array_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "failed to decode `uint64_t_vals_required`: type is not array"));
    priskv_codec_destroy(codec);
}

static void test_codec_decode_not_uint64_t_array()
{
    priskv_codec *codec = priskv_codec_new();
    char test_uint64_t_array_json[] = "{ \"uint64_t_vals_required\": [true, false] }";
    struct test_uint64_t_array *test_uint64_t_array_tmp =
        priskv_codec_decode(codec, test_uint64_t_array_json, &test_uint64_t_array_obj);

    assert(test_uint64_t_array_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "failed to decode array `uint64_t_vals_required`: type is not int"));
    priskv_codec_destroy(codec);
}

static void test_codec_decode_uint64_t_array_optional()
{
    priskv_codec *codec = priskv_codec_new();
    char test_uint64_t_array_json[] = "{ \"uint64_t_vals_required\": [ 1, 2, 3 ] }";
    uint64_t test_uint64_ts_val_required[] = {1, 2, 3};
    struct test_uint64_t_array test_uint64_t_array = {.uint64_t_vals_required =
                                                          test_uint64_ts_val_required,
                                                      .nuint64_t_vals_required = 3};
    struct test_uint64_t_array *test_uint64_t_array_tmp =
        priskv_codec_decode(codec, test_uint64_t_array_json, &test_uint64_t_array_obj);

    assert(test_uint64_t_array_tmp != NULL);
    assert(test_uint64_t_array_tmp->nuint64_t_vals_required ==
           test_uint64_t_array.nuint64_t_vals_required);
    for (int i = 0; i < test_uint64_t_array.nuint64_t_vals_required; i++) {
        assert(test_uint64_t_array_tmp->uint64_t_vals_required[i] ==
               test_uint64_t_array.uint64_t_vals_required[i]);
    }

    assert(test_uint64_t_array_tmp->nuint64_t_vals_optional == 0);
    assert(test_uint64_t_array_tmp->uint64_t_vals_optional == NULL);

    assert(!priskv_codec_free_struct(codec, test_uint64_t_array_tmp, &test_uint64_t_array_obj));

    priskv_codec_destroy(codec);
}

static void test_codec_decode_uint64_t_array_missing_required()
{
    priskv_codec *codec = priskv_codec_new();
    char test_uint64_t_array_json[] = "{ \"uint64_t_vals_optional\": [ 4, 5 ] }";
    struct test_uint64_t_array *test_uint64_t_array_tmp =
        priskv_codec_decode(codec, test_uint64_t_array_json, &test_uint64_t_array_obj);

    assert(test_uint64_t_array_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "not found `uint64_t_vals_required` that is required"));

    priskv_codec_destroy(codec);
}

static void test_codec_code_uint64_t_array_ignored()
{
    priskv_codec *codec = priskv_codec_new();
    uint64_t test_uint64_ts_val_required[] = {1, 2, 3};
    struct test_uint64_t_array test_uint64_t_array = {.uint64_t_vals_required =
                                                          test_uint64_ts_val_required,
                                                      .nuint64_t_vals_required = 3,
                                                      .uint64_t_vals_optional = NULL,
                                                      .nuint64_t_vals_optional = 0};

    char *str = priskv_codec_code(codec, &test_uint64_t_array, &test_uint64_t_array_ignored_obj);
    assert(!strcmp(str, "{ \"uint64_t_vals_required\": [ 1, 2, 3 ] }"));
    free(str);

    priskv_codec_destroy(codec);
}

static void test_codec_code_uint64_t_array_forced()
{
    priskv_codec *codec = priskv_codec_new();
    struct test_uint64_t_array test_uint64_t_array = {.uint64_t_vals_required = NULL,
                                                      .nuint64_t_vals_required = 0,
                                                      .uint64_t_vals_optional = NULL,
                                                      .nuint64_t_vals_optional = 0};

    char *str = priskv_codec_code(codec, &test_uint64_t_array, &test_uint64_t_array_ignored_obj);
    assert(!strcmp(str, "{ \"uint64_t_vals_required\": [ ] }"));
    free(str);

    priskv_codec_destroy(codec);
}

struct test_boolean_array {
    bool *boolean_vals_required;
    int nboolean_vals_required;
    bool *boolean_vals_optional;
    int nboolean_vals_optional;
};

PRISKV_DECL_OBJECT_BEGIN(test_boolean_array)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_boolean_array, "boolean_vals_required",
                             boolean_vals_required, nboolean_vals_required, priskv_boolean, required,
                             forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_boolean_array, "boolean_vals_optional",
                             boolean_vals_optional, nboolean_vals_optional, priskv_boolean, optional,
                             forced)
PRISKV_DECL_OBJECT_END(test_boolean_array, struct test_boolean_array)

PRISKV_DECL_OBJECT_BEGIN(test_boolean_array_ignored)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_boolean_array, "boolean_vals_required",
                             boolean_vals_required, nboolean_vals_required, priskv_boolean, required,
                             forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_boolean_array, "boolean_vals_optional",
                             boolean_vals_optional, nboolean_vals_optional, priskv_boolean, optional,
                             ignored)
PRISKV_DECL_OBJECT_END(test_boolean_array_ignored, struct test_boolean_array)

static void test_codec_decode_and_code_boolean_array()
{
    priskv_codec *codec = priskv_codec_new();
    char test_boolean_array_json[] = "{ \"boolean_vals_required\": [ true, false, true ], "
                                     "\"boolean_vals_optional\": [ false, true ] }";
    bool test_booleans_val_required[] = {true, false, true};
    bool test_booleans_val_optional[] = {false, true};
    struct test_boolean_array test_boolean_array = {
        .boolean_vals_required = test_booleans_val_required,
        .nboolean_vals_required = 3,
        .boolean_vals_optional = test_booleans_val_optional,
        .nboolean_vals_optional = 2};
    struct test_boolean_array *test_boolean_array_tmp =
        priskv_codec_decode(codec, test_boolean_array_json, &test_boolean_array_obj);

    assert(test_boolean_array_tmp != NULL);
    assert(test_boolean_array_tmp->nboolean_vals_required ==
           test_boolean_array.nboolean_vals_required);
    for (int i = 0; i < test_boolean_array.nboolean_vals_required; i++) {
        assert(test_boolean_array_tmp->boolean_vals_required[i] ==
               test_boolean_array.boolean_vals_required[i]);
    }

    assert(test_boolean_array_tmp->nboolean_vals_optional ==
           test_boolean_array.nboolean_vals_optional);
    for (int i = 0; i < test_boolean_array.nboolean_vals_optional; i++) {
        assert(test_boolean_array_tmp->boolean_vals_optional[i] ==
               test_boolean_array.boolean_vals_optional[i]);
    }

    assert(!priskv_codec_free_struct(codec, test_boolean_array_tmp, &test_boolean_array_obj));

    char *str = priskv_codec_code(codec, &test_boolean_array, &test_boolean_array_obj);
    assert(!strcmp(str, test_boolean_array_json));
    free(str);

    priskv_codec_destroy(codec);
}

static void test_codec_decode_boolean_not_array()
{
    priskv_codec *codec = priskv_codec_new();
    char test_boolean_array_json[] = "{ \"boolean_vals_required\": true }";
    struct test_boolean_array *test_boolean_array_tmp =
        priskv_codec_decode(codec, test_boolean_array_json, &test_boolean_array_obj);

    assert(test_boolean_array_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "failed to decode `boolean_vals_required`: type is not array"));
    priskv_codec_destroy(codec);
}

static void test_codec_decode_not_boolean_array()
{
    priskv_codec *codec = priskv_codec_new();
    char test_boolean_array_json[] = "{ \"boolean_vals_required\": [\"string1\", \"string2\"] }";
    struct test_boolean_array *test_boolean_array_tmp =
        priskv_codec_decode(codec, test_boolean_array_json, &test_boolean_array_obj);

    assert(test_boolean_array_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "failed to decode array `boolean_vals_required`: type is not boolean"));
    priskv_codec_destroy(codec);
}

static void test_codec_decode_boolean_array_optional()
{
    priskv_codec *codec = priskv_codec_new();
    char test_boolean_array_json[] = "{ \"boolean_vals_required\": [ true, false, true ] }";
    bool test_booleans_val_required[] = {true, false, true};
    struct test_boolean_array test_boolean_array = {.boolean_vals_required =
                                                        test_booleans_val_required,
                                                    .nboolean_vals_required = 3};
    struct test_boolean_array *test_boolean_array_tmp =
        priskv_codec_decode(codec, test_boolean_array_json, &test_boolean_array_obj);

    assert(test_boolean_array_tmp != NULL);
    assert(test_boolean_array_tmp->nboolean_vals_required ==
           test_boolean_array.nboolean_vals_required);
    for (int i = 0; i < test_boolean_array.nboolean_vals_required; i++) {
        assert(test_boolean_array_tmp->boolean_vals_required[i] ==
               test_boolean_array.boolean_vals_required[i]);
    }

    assert(test_boolean_array_tmp->nboolean_vals_optional == 0);
    assert(test_boolean_array_tmp->boolean_vals_optional == NULL);

    assert(!priskv_codec_free_struct(codec, test_boolean_array_tmp, &test_boolean_array_obj));

    priskv_codec_destroy(codec);
}

static void test_codec_decode_boolean_array_missing_required()
{
    priskv_codec *codec = priskv_codec_new();
    char test_boolean_array_json[] = "{ \"boolean_vals_optional\": [ false, true ] }";
    struct test_boolean_array *test_boolean_array_tmp =
        priskv_codec_decode(codec, test_boolean_array_json, &test_boolean_array_obj);

    assert(test_boolean_array_tmp == NULL);
    assert(
        !strcmp(priskv_codec_get_error(codec), "not found `boolean_vals_required` that is required"));

    priskv_codec_destroy(codec);
}

static void test_codec_code_boolean_array_ignored()
{
    priskv_codec *codec = priskv_codec_new();
    bool test_booleans_val_required[] = {true, false, true};
    struct test_boolean_array test_boolean_array = {.boolean_vals_required =
                                                        test_booleans_val_required,
                                                    .nboolean_vals_required = 3,
                                                    .boolean_vals_optional = NULL,
                                                    .nboolean_vals_optional = 0};

    char *str = priskv_codec_code(codec, &test_boolean_array, &test_boolean_array_ignored_obj);
    assert(!strcmp(str, "{ \"boolean_vals_required\": [ true, false, true ] }"));
    free(str);

    priskv_codec_destroy(codec);
}

static void test_codec_code_boolean_array_forced()
{
    priskv_codec *codec = priskv_codec_new();
    struct test_boolean_array test_boolean_array = {.boolean_vals_required = NULL,
                                                    .nboolean_vals_required = 0,
                                                    .boolean_vals_optional = NULL,
                                                    .nboolean_vals_optional = 0};

    char *str = priskv_codec_code(codec, &test_boolean_array, &test_boolean_array_ignored_obj);
    assert(!strcmp(str, "{ \"boolean_vals_required\": [ ] }"));
    free(str);

    priskv_codec_destroy(codec);
}

struct test_string_array {
    const char **string_vals_required;
    int nstring_vals_required;
    const char **string_vals_optional;
    int nstring_vals_optional;
};

PRISKV_DECL_OBJECT_BEGIN(test_string_array)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_string_array, "string_vals_required", string_vals_required,
                             nstring_vals_required, priskv_string, required, forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_string_array, "string_vals_optional", string_vals_optional,
                             nstring_vals_optional, priskv_string, optional, forced)
PRISKV_DECL_OBJECT_END(test_string_array, struct test_string_array)

PRISKV_DECL_OBJECT_BEGIN(test_string_array_ignored)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_string_array, "string_vals_required", string_vals_required,
                             nstring_vals_required, priskv_string, required, forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_string_array, "string_vals_optional", string_vals_optional,
                             nstring_vals_optional, priskv_string, optional, ignored)
PRISKV_DECL_OBJECT_END(test_string_array_ignored, struct test_string_array)

static void test_codec_decode_and_code_string_array()
{
    priskv_codec *codec = priskv_codec_new();
    char test_string_array_json[] =
        "{ \"string_vals_required\": [ \"string1\", \"string2\", \"string3\" ], "
        "\"string_vals_optional\": [ \"string4\", \"string5\" ] }";
    const char *test_strings_val_required[] = {"string1", "string2", "string3"};
    const char *test_strings_val_optional[] = {"string4", "string5"};
    struct test_string_array test_string_array = {.string_vals_required = test_strings_val_required,
                                                  .nstring_vals_required = 3,
                                                  .string_vals_optional = test_strings_val_optional,
                                                  .nstring_vals_optional = 2};
    struct test_string_array *test_string_array_tmp =
        priskv_codec_decode(codec, test_string_array_json, &test_string_array_obj);

    assert(test_string_array_tmp != NULL);
    assert(test_string_array_tmp->nstring_vals_required == test_string_array.nstring_vals_required);
    for (int i = 0; i < test_string_array.nstring_vals_required; i++) {
        assert(!strcmp(test_string_array_tmp->string_vals_required[i],
                       test_string_array.string_vals_required[i]));
    }

    assert(test_string_array_tmp->nstring_vals_optional == test_string_array.nstring_vals_optional);
    for (int i = 0; i < test_string_array.nstring_vals_optional; i++) {
        assert(!strcmp(test_string_array_tmp->string_vals_optional[i],
                       test_string_array.string_vals_optional[i]));
    }

    assert(!priskv_codec_free_struct(codec, test_string_array_tmp, &test_string_array_obj));

    char *str = priskv_codec_code(codec, &test_string_array, &test_string_array_obj);
    assert(!strcmp(str, test_string_array_json));
    free(str);

    priskv_codec_destroy(codec);
}

static void test_codec_decode_string_not_array()
{
    priskv_codec *codec = priskv_codec_new();
    char test_string_array_json[] = "{ \"string_vals_required\": \"string1\" }";
    struct test_string_array *test_string_array_tmp =
        priskv_codec_decode(codec, test_string_array_json, &test_string_array_obj);

    assert(test_string_array_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "failed to decode `string_vals_required`: type is not array"));
    priskv_codec_destroy(codec);
}

static void test_codec_decode_not_string_array()
{
    priskv_codec *codec = priskv_codec_new();
    char test_string_array_json[] = "{ \"string_vals_required\": [true, false] }";
    struct test_string_array *test_string_array_tmp =
        priskv_codec_decode(codec, test_string_array_json, &test_string_array_obj);

    assert(test_string_array_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "failed to decode array `string_vals_required`: type is not string"));
    priskv_codec_destroy(codec);
}

static void test_codec_decode_string_array_optional()
{
    priskv_codec *codec = priskv_codec_new();
    char test_string_array_json[] =
        "{ \"string_vals_required\": [ \"string1\", \"string2\", \"string3\" ] }";
    const char *test_strings_val_required[] = {"string1", "string2", "string3"};
    struct test_string_array test_string_array = {.string_vals_required = test_strings_val_required,
                                                  .nstring_vals_required = 3};
    struct test_string_array *test_string_array_tmp =
        priskv_codec_decode(codec, test_string_array_json, &test_string_array_obj);

    assert(test_string_array_tmp != NULL);
    assert(test_string_array_tmp->nstring_vals_required == test_string_array.nstring_vals_required);
    for (int i = 0; i < test_string_array.nstring_vals_required; i++) {
        assert(!strcmp(test_string_array_tmp->string_vals_required[i],
                       test_string_array.string_vals_required[i]));
    }

    assert(test_string_array_tmp->nstring_vals_optional == 0);
    assert(test_string_array_tmp->string_vals_optional == NULL);

    assert(!priskv_codec_free_struct(codec, test_string_array_tmp, &test_string_array_obj));

    priskv_codec_destroy(codec);
}

static void test_codec_decode_string_array_missing_required()
{
    priskv_codec *codec = priskv_codec_new();
    char test_string_array_json[] = "{ \"string_vals_optional\": [ \"string4\", \"string5\" ] }";
    struct test_string_array *test_string_array_tmp =
        priskv_codec_decode(codec, test_string_array_json, &test_string_array_obj);

    assert(test_string_array_tmp == NULL);
    assert(
        !strcmp(priskv_codec_get_error(codec), "not found `string_vals_required` that is required"));

    priskv_codec_destroy(codec);
}

static void test_codec_code_string_array_ignored()
{
    priskv_codec *codec = priskv_codec_new();
    const char *test_strings_val_required[] = {"string1", "string2", "string3"};
    struct test_string_array test_string_array = {.string_vals_required = test_strings_val_required,
                                                  .nstring_vals_required = 3,
                                                  .string_vals_optional = NULL,
                                                  .nstring_vals_optional = 0};

    char *str = priskv_codec_code(codec, &test_string_array, &test_string_array_ignored_obj);
    assert(!strcmp(str, "{ \"string_vals_required\": [ \"string1\", \"string2\", \"string3\" ] }"));
    free(str);

    priskv_codec_destroy(codec);
}

static void test_codec_code_string_array_forced()
{
    priskv_codec *codec = priskv_codec_new();
    struct test_string_array test_string_array = {.string_vals_required = NULL,
                                                  .nstring_vals_required = 0,
                                                  .string_vals_optional = NULL,
                                                  .nstring_vals_optional = 0};

    char *str = priskv_codec_code(codec, &test_string_array, &test_string_array_ignored_obj);
    assert(!strcmp(str, "{ \"string_vals_required\": [ ] }"));
    free(str);

    priskv_codec_destroy(codec);
}

struct test_struct_array {
    struct test_struct_child *struct_vals_required;
    int nstruct_vals_required;
    struct test_struct_child *struct_vals_optional;
    int nstruct_vals_optional;
};

PRISKV_DECL_OBJECT_BEGIN(test_struct_array)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_struct_array, "struct_vals_required", struct_vals_required,
                             nstruct_vals_required, test_struct_child, required, forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_struct_array, "struct_vals_optional", struct_vals_optional,
                             nstruct_vals_optional, test_struct_child, optional, forced)
PRISKV_DECL_OBJECT_END(test_struct_array, struct test_struct_array)

PRISKV_DECL_OBJECT_BEGIN(test_struct_array_ignored)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_struct_array, "struct_vals_required", struct_vals_required,
                             nstruct_vals_required, test_struct_child, required, forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct test_struct_array, "struct_vals_optional", struct_vals_optional,
                             nstruct_vals_optional, test_struct_child, optional, ignored)
PRISKV_DECL_OBJECT_END(test_struct_array_ignored, struct test_struct_array)

static void test_codec_decode_and_code_struct_array()
{
    priskv_codec *codec = priskv_codec_new();
    char test_struct_array_json[] =
        "{ \"struct_vals_required\": [ { \"val\": 1 }, { \"val\": 2 }, { \"val\": 3 } ], "
        "\"struct_vals_optional\": [ { \"val\": 4 }, { \"val\": 5 } ] }";
    struct test_struct_child test_structs_val_required[] = {{1}, {2}, {3}};
    struct test_struct_child test_structs_val_optional[] = {{4}, {5}};
    struct test_struct_array test_struct_array = {.struct_vals_required = test_structs_val_required,
                                                  .nstruct_vals_required = 3,
                                                  .struct_vals_optional = test_structs_val_optional,
                                                  .nstruct_vals_optional = 2};
    struct test_struct_array *test_struct_array_tmp =
        priskv_codec_decode(codec, test_struct_array_json, &test_struct_array_obj);

    assert(test_struct_array_tmp != NULL);
    assert(test_struct_array_tmp->nstruct_vals_required == test_struct_array.nstruct_vals_required);
    for (int i = 0; i < test_struct_array.nstruct_vals_required; i++) {
        assert(test_struct_array_tmp->struct_vals_required[i].val ==
               test_struct_array.struct_vals_required[i].val);
    }

    assert(test_struct_array_tmp->nstruct_vals_optional == test_struct_array.nstruct_vals_optional);
    for (int i = 0; i < test_struct_array.nstruct_vals_optional; i++) {
        assert(test_struct_array_tmp->struct_vals_optional[i].val ==
               test_struct_array.struct_vals_optional[i].val);
    }

    assert(!priskv_codec_free_struct(codec, test_struct_array_tmp, &test_struct_array_obj));

    char *str = priskv_codec_code(codec, &test_struct_array, &test_struct_array_obj);
    assert(!strcmp(str, test_struct_array_json));
    free(str);

    priskv_codec_destroy(codec);
}

static void test_codec_decode_struct_not_array()
{
    priskv_codec *codec = priskv_codec_new();
    char test_struct_array_json[] = "{ \"struct_vals_required\": 1 }";
    struct test_struct_array *test_struct_array_tmp =
        priskv_codec_decode(codec, test_struct_array_json, &test_struct_array_obj);

    assert(test_struct_array_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "failed to decode `struct_vals_required`: type is not array"));
    priskv_codec_destroy(codec);
}

static void test_codec_decode_not_struct_array()
{
    priskv_codec *codec = priskv_codec_new();
    char test_struct_array_json[] = "{ \"struct_vals_required\": [true, false] }";
    struct test_struct_array *test_struct_array_tmp =
        priskv_codec_decode(codec, test_struct_array_json, &test_struct_array_obj);

    assert(test_struct_array_tmp == NULL);
    assert(!strcmp(priskv_codec_get_error(codec),
                   "failed to decode array `struct_vals_required`: type is not object"));
    priskv_codec_destroy(codec);
}

static void test_codec_decode_struct_array_optional()
{
    priskv_codec *codec = priskv_codec_new();
    char test_struct_array_json[] =
        "{ \"struct_vals_required\": [ { \"val\": 1 }, { \"val\": 2 }, { \"val\": 3 } ] }";
    struct test_struct_child test_structs_val_required[] = {{1}, {2}, {3}};
    struct test_struct_array test_struct_array = {.struct_vals_required = test_structs_val_required,
                                                  .nstruct_vals_required = 3};
    struct test_struct_array *test_struct_array_tmp =
        priskv_codec_decode(codec, test_struct_array_json, &test_struct_array_obj);

    assert(test_struct_array_tmp != NULL);
    assert(test_struct_array_tmp->nstruct_vals_required == test_struct_array.nstruct_vals_required);
    for (int i = 0; i < test_struct_array.nstruct_vals_required; i++) {
        assert(test_struct_array_tmp->struct_vals_required[i].val ==
               test_struct_array.struct_vals_required[i].val);
    }

    assert(test_struct_array_tmp->nstruct_vals_optional == 0);
    assert(test_struct_array_tmp->struct_vals_optional == NULL);

    assert(!priskv_codec_free_struct(codec, test_struct_array_tmp, &test_struct_array_obj));

    priskv_codec_destroy(codec);
}

static void test_codec_decode_struct_array_missing_required()
{
    priskv_codec *codec = priskv_codec_new();
    char test_struct_array_json[] =
        "{ \"struct_vals_optional\": [ { \"val\": 4 }, { \"val\": 5 } ] }";
    struct test_struct_array *test_struct_array_tmp =
        priskv_codec_decode(codec, test_struct_array_json, &test_struct_array_obj);

    assert(test_struct_array_tmp == NULL);
    assert(
        !strcmp(priskv_codec_get_error(codec), "not found `struct_vals_required` that is required"));

    priskv_codec_destroy(codec);
}

static void test_codec_code_struct_array_ignored()
{
    priskv_codec *codec = priskv_codec_new();
    struct test_struct_child test_structs_val_required[] = {{1}, {2}, {3}};
    struct test_struct_array test_struct_array = {.struct_vals_required = test_structs_val_required,
                                                  .nstruct_vals_required = 3,
                                                  .struct_vals_optional = NULL,
                                                  .nstruct_vals_optional = 0};

    char *str = priskv_codec_code(codec, &test_struct_array, &test_struct_array_ignored_obj);
    assert(!strcmp(
        str, "{ \"struct_vals_required\": [ { \"val\": 1 }, { \"val\": 2 }, { \"val\": 3 } ] }"));
    free(str);

    priskv_codec_destroy(codec);
}

static void test_codec_code_struct_array_forced()
{
    priskv_codec *codec = priskv_codec_new();
    struct test_struct_array test_struct_array = {.struct_vals_required = NULL,
                                                  .nstruct_vals_required = 0,
                                                  .struct_vals_optional = NULL,
                                                  .nstruct_vals_optional = 0};

    char *str = priskv_codec_code(codec, &test_struct_array, &test_struct_array_ignored_obj);
    assert(!strcmp(str, "{ \"struct_vals_required\": [ ] }"));
    free(str);

    priskv_codec_destroy(codec);
}

struct child {
    int int_val;
    uint64_t uint64_val;
    const char *string;
    bool boolean;

    int *int_vals;
    int nint_vals;

    uint64_t *uint64_vals;
    int nuint64_vals;

    const char **strings;
    int nstrings;

    bool *booleans;
    int nbooleans;
};

PRISKV_DECL_OBJECT_BEGIN(child)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct child, "int_val", int_val, priskv_int, required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct child, "uint64_val", uint64_val, priskv_uint64, required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct child, "boolean_val", boolean, priskv_boolean, required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct child, "string_val", string, priskv_string, required, forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct child, "int_vals", int_vals, nint_vals, priskv_int, required,
                             forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct child, "uint64_vals", uint64_vals, nuint64_vals, priskv_uint64,
                             required, forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct child, "string_vals", strings, nstrings, priskv_string, required,
                             forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct child, "boolean_vals", booleans, nbooleans, priskv_boolean,
                             required, forced)
PRISKV_DECL_OBJECT_END(child, struct child)

struct parent {
    int int_val;
    uint64_t uint64_val;
    const char *string;
    bool boolean;
    struct child c;

    int *int_vals;
    int nint_vals;

    uint64_t *uint64_vals;
    int nuint64_vals;

    const char **strings;
    int nstrings;

    bool *booleans;
    int nbooleans;

    struct child *cs;
    int ncs;
};

PRISKV_DECL_OBJECT_BEGIN(parent)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct parent, "int_val", int_val, priskv_int, required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct parent, "uint64_val", uint64_val, priskv_uint64, required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct parent, "boolean_val", boolean, priskv_boolean, required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct parent, "string_val", string, priskv_string, required, forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(struct parent, "child", c, child, required, forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct parent, "int_vals", int_vals, nint_vals, priskv_int, required,
                             forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct parent, "uint64_vals", uint64_vals, nuint64_vals, priskv_uint64,
                             required, forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct parent, "string_vals", strings, nstrings, priskv_string, required,
                             forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct parent, "boolean_vals", booleans, nbooleans, priskv_boolean,
                             required, forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(struct parent, "childs", cs, ncs, child, required, forced)
PRISKV_DECL_OBJECT_END(parent, struct parent)

static int compare_child(struct child *c1, struct child *c2)
{
    if (c1->int_val != c2->int_val) {
        return 1;
    }

    if (c1->uint64_val != c2->uint64_val) {
        return 1;
    }

    if (c1->boolean != c2->boolean) {
        return 1;
    }

    if (strcmp(c1->string, c2->string)) {
        return 1;
    }

    if (c1->nint_vals != c2->nint_vals) {
        return 1;
    }
    for (int i = 0; i < c1->nint_vals; i++) {
        if (c1->int_vals[i] != c2->int_vals[i]) {
            return 1;
        }
    }

    if (c1->nuint64_vals != c2->nuint64_vals) {
        return 1;
    }
    for (int i = 0; i < c1->nuint64_vals; i++) {
        if (c1->uint64_vals[i] != c2->uint64_vals[i]) {
            return 1;
        }
    }

    if (c1->nstrings != c2->nstrings) {
        return 1;
    }
    for (int i = 0; i < c1->nstrings; i++) {
        if (strcmp(c1->strings[i], c2->strings[i])) {
            return 1;
        }
    }

    if (c1->nbooleans != c2->nbooleans) {
        return 1;
    }
    for (int i = 0; i < c1->nbooleans; i++) {
        if (c1->booleans[i] != c2->booleans[i]) {
            return 1;
        }
    }

    return 0;
}

static int compare_parent(struct parent *p1, struct parent *p2)
{
    if (p1->int_val != p2->int_val) {
        return 1;
    }

    if (p1->uint64_val != p2->uint64_val) {
        return 1;
    }

    if (p1->boolean != p2->boolean) {
        return 1;
    }

    if (strcmp(p1->string, p2->string)) {
        return 1;
    }

    if (compare_child(&p1->c, &p2->c)) {
        return 1;
    }

    if (p1->nint_vals != p2->nint_vals) {
        return 1;
    }
    for (int i = 0; i < p1->nint_vals; i++) {
        if (p1->int_vals[i] != p2->int_vals[i]) {
            return 1;
        }
    }

    if (p1->nuint64_vals != p2->nuint64_vals) {
        return 1;
    }
    for (int i = 0; i < p1->nuint64_vals; i++) {
        if (p1->uint64_vals[i] != p2->uint64_vals[i]) {
            return 1;
        }
    }

    if (p1->nstrings != p2->nstrings) {
        return 1;
    }
    for (int i = 0; i < p1->nstrings; i++) {
        if (strcmp(p1->strings[i], p2->strings[i])) {
            return 1;
        }
    }

    if (p1->nbooleans != p2->nbooleans) {
        return 1;
    }
    for (int i = 0; i < p1->nbooleans; i++) {
        if (p1->booleans[i] != p2->booleans[i]) {
            return 1;
        }
    }

    if (p1->ncs != p2->ncs) {
        return 1;
    }
    for (int i = 0; i < p1->ncs; i++) {
        if (compare_child(&p1->cs[i], &p2->cs[i])) {
            return 1;
        }
    }

    return 0;
}

static void test_codec_integrate()
{
    char json_str[] = " \
{ \
    \"int_val\": 101010101, \
    \"uint64_val\": 207374182402, \
    \"boolean_val\": false, \
    \"string_val\": \"this is parent\", \
    \"child\": { \
        \"int_val\": 111111, \
        \"uint64_val\": 107374182400, \
        \"boolean_val\": true, \
        \"string_val\": \"this is child A\", \
        \"int_vals\": [ \
            111111, \
            111112, \
            111113 \
        ], \
        \"uint64_vals\": [ \
            107374182401, \
            107374182402 \
        ], \
        \"string_vals\": [ \
            \"this is child A1\", \
            \"this is child A2\", \
            \"this is child A3\" \
        ], \
        \"boolean_vals\": [ \
            true, \
            false, \
            true \
        ] \
    }, \
    \"int_vals\": [ \
        101010101, \
        202020202, \
        303030303 \
    ], \
    \"uint64_vals\": [ \
        217374182402, \
        227374182402 \
    ], \
    \"string_vals\": [ \
        \"this is parent 1\", \
        \"this is parent 2\", \
        \"this is parent 3\" \
    ], \
    \"boolean_vals\": [ \
        false, \
        true, \
        true \
    ], \
    \"childs\": [ \
        { \
            \"int_val\": 222222, \
            \"uint64_val\": 117374182400, \
            \"boolean_val\": true, \
            \"string_val\": \"this is child B\", \
            \"int_vals\": [ \
                222221, \
                222222, \
                222223 \
            ], \
            \"uint64_vals\": [ \
                117374182401, \
                117374182402 \
            ], \
            \"string_vals\": [ \
                \"this is child B1\", \
                \"this is child B2\", \
                \"this is child B3\" \
            ], \
            \"boolean_vals\": [ \
                true, \
                false, \
                true \
            ] \
        }, \
        { \
            \"int_val\": 333333, \
            \"uint64_val\": 137374182400, \
            \"boolean_val\": true, \
            \"string_val\": \"this is child C\", \
            \"int_vals\": [ \
                333331, \
                333332, \
                333333 \
            ], \
            \"uint64_vals\": [ \
                137374182401, \
                137374182402 \
            ], \
            \"string_vals\": [ \
                \"this is child C1\", \
                \"this is child C2\", \
                \"this is child C3\" \
            ], \
            \"boolean_vals\": [ \
                true, \
                false, \
                true \
            ] \
        } \
    ] \
} ";

    int val1[3] = {111111, 111112, 111113};
    uint64_t val2[2] = {107374182401, 107374182402};
    const char *val3[3] = {"this is child A1", "this is child A2", "this is child A3"};
    bool val4[3] = {true, false, true};

    int val5[3] = {101010101, 202020202, 303030303};
    uint64_t val6[2] = {217374182402, 227374182402};
    const char *val7[3] = {"this is parent 1", "this is parent 2", "this is parent 3"};
    bool val8[3] = {false, true, true};

    int val9[3] = {222221, 222222, 222223};
    uint64_t val10[2] = {117374182401, 117374182402};
    const char *val11[3] = {"this is child B1", "this is child B2", "this is child B3"};
    bool val12[3] = {true, false, true};

    int val13[3] = {333331, 333332, 333333};
    uint64_t val14[2] = {137374182401, 137374182402};
    const char *val15[3] = {"this is child C1", "this is child C2", "this is child C3"};
    bool val16[3] = {true, false, true};

    struct child cs[2] = {{
                              .int_val = 222222,
                              .uint64_val = 117374182400,
                              .string = "this is child B",
                              .boolean = true,
                              .nint_vals = 3,
                              .int_vals = val9,
                              .nuint64_vals = 2,
                              .uint64_vals = val10,
                              .nstrings = 3,
                              .strings = val11,
                              .nbooleans = 3,
                              .booleans = val12,
                          },
                          {
                              .int_val = 333333,
                              .uint64_val = 137374182400,
                              .string = "this is child C",
                              .boolean = true,
                              .nint_vals = 3,
                              .int_vals = val13,
                              .nuint64_vals = 2,
                              .uint64_vals = val14,
                              .nstrings = 3,
                              .strings = val15,
                              .nbooleans = 3,
                              .booleans = val16,
                          }};

    struct parent parent_target = {
        .int_val = 101010101,
        .uint64_val = 207374182402,
        .boolean = false,
        .string = "this is parent",
        .c =
            {
                .int_val = 111111,
                .uint64_val = 107374182400,
                .boolean = true,
                .string = "this is child A",
                .nint_vals = 3,
                .int_vals = val1,
                .nuint64_vals = 2,
                .uint64_vals = val2,
                .nstrings = 3,
                .strings = val3,
                .nbooleans = 3,
                .booleans = val4,
            },
        .nint_vals = 3,
        .int_vals = val5,
        .nuint64_vals = 2,
        .uint64_vals = val6,
        .nstrings = 3,
        .strings = val7,
        .nbooleans = 3,
        .booleans = val8,
        .ncs = 2,
        .cs = cs,
    };

    priskv_codec *codec = priskv_codec_new();

    struct parent *p = priskv_codec_decode(codec, json_str, &parent_obj);
    assert(!compare_parent(p, &parent_target));
    assert(!priskv_codec_free_struct(codec, p, &parent_obj));

    char *str = priskv_codec_code(codec, &parent_target, &parent_obj);
    strtrim(str, ' ');
    strtrim(json_str, ' ');
    assert(!strcmp(str, json_str));
    free(str);

    priskv_codec_destroy(codec);
}

int main()
{
    priskv_set_log_level(priskv_log_info);

    priskv_unittest_t tests[] = {
        priskv_unittest(test_codec_new_and_destroy),
        /* test for `int` */
        priskv_unittest(test_codec_decode_and_code_int),
        priskv_unittest(test_codec_decode_not_int),
        priskv_unittest(test_codec_decode_int_optional),
        priskv_unittest(test_codec_decode_int_missing_required),
        priskv_unittest(test_codec_code_int_ignored),
        /* test for `uint64_t` */
        priskv_unittest(test_codec_decode_and_code_uint64_t),
        priskv_unittest(test_codec_decode_not_uint64_t),
        priskv_unittest(test_codec_decode_uint64_t_optional),
        priskv_unittest(test_codec_decode_uint64_t_missing_required),
        priskv_unittest(test_codec_code_uint64_t_ignored),
        /* test for `boolean` */
        priskv_unittest(test_codec_decode_and_code_boolean),
        priskv_unittest(test_codec_decode_not_boolean),
        priskv_unittest(test_codec_decode_boolean_optional),
        priskv_unittest(test_codec_decode_boolean_missing_required),
        priskv_unittest(test_codec_code_boolean_ignored),
        /* test for `string` */
        priskv_unittest(test_codec_decode_and_code_string),
        priskv_unittest(test_codec_decode_not_string),
        priskv_unittest(test_codec_decode_string_optional),
        priskv_unittest(test_codec_decode_string_missing_required),
        priskv_unittest(test_codec_code_string_ignored),
        /* test for `struct` */
        priskv_unittest(test_codec_decode_and_code_struct),
        priskv_unittest(test_codec_decode_not_struct),
        priskv_unittest(test_codec_decode_struct_optional),
        priskv_unittest(test_codec_decode_struct_missing_required),
        /* test for `int` array */
        priskv_unittest(test_codec_decode_and_code_int_array),
        priskv_unittest(test_codec_decode_int_not_array),
        priskv_unittest(test_codec_decode_not_int_array),
        priskv_unittest(test_codec_decode_int_array_optional),
        priskv_unittest(test_codec_decode_int_array_missing_required),
        priskv_unittest(test_codec_code_int_array_ignored),
        priskv_unittest(test_codec_code_int_array_forced),
        /* test for `uint64_t` array */
        priskv_unittest(test_codec_decode_and_code_uint64_t_array),
        priskv_unittest(test_codec_decode_uint64_t_not_array),
        priskv_unittest(test_codec_decode_not_uint64_t_array),
        priskv_unittest(test_codec_decode_uint64_t_array_optional),
        priskv_unittest(test_codec_decode_uint64_t_array_missing_required),
        priskv_unittest(test_codec_code_uint64_t_array_ignored),
        priskv_unittest(test_codec_code_uint64_t_array_forced),
        /* test for `boolean` array */
        priskv_unittest(test_codec_decode_and_code_boolean_array),
        priskv_unittest(test_codec_decode_boolean_not_array),
        priskv_unittest(test_codec_decode_not_boolean_array),
        priskv_unittest(test_codec_decode_boolean_array_optional),
        priskv_unittest(test_codec_decode_boolean_array_missing_required),
        priskv_unittest(test_codec_code_boolean_array_ignored),
        priskv_unittest(test_codec_code_boolean_array_forced),
        /* test for `string` array */
        priskv_unittest(test_codec_decode_and_code_string_array),
        priskv_unittest(test_codec_decode_string_not_array),
        priskv_unittest(test_codec_decode_not_string_array),
        priskv_unittest(test_codec_decode_string_array_optional),
        priskv_unittest(test_codec_decode_string_array_missing_required),
        priskv_unittest(test_codec_code_string_array_ignored),
        priskv_unittest(test_codec_code_string_array_forced),
        /* test for `struct` array */
        priskv_unittest(test_codec_decode_and_code_struct_array),
        priskv_unittest(test_codec_decode_struct_not_array),
        priskv_unittest(test_codec_decode_not_struct_array),
        priskv_unittest(test_codec_decode_struct_array_optional),
        priskv_unittest(test_codec_decode_struct_array_missing_required),
        priskv_unittest(test_codec_code_struct_array_ignored),
        priskv_unittest(test_codec_code_struct_array_forced),

        priskv_unittest(test_codec_integrate),
    };

    priskv_unittest_run_all(tests);

    return 0;
}
