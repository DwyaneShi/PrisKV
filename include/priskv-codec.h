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

#ifndef __PRISKV_CODEC_H__
#define __PRISKV_CODEC_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "json-c/json.h"

typedef struct priskv_codec priskv_codec;

typedef enum priskv_object_type {
    priskv_obj_type_boolean = 0,
    priskv_obj_type_int,
    priskv_obj_type_uint64,
    priskv_obj_type_string,
    priskv_obj_type_object,
    priskv_obj_type_max,
} priskv_object_type;

typedef enum priskv_object_field_type {
    priskv_obj_field_type_value = 0,
    priskv_obj_field_type_array,
} priskv_object_field_type;

typedef struct priskv_object priskv_object;
typedef struct priskv_object_field priskv_object_field;

struct priskv_object {
    priskv_object_type type;
    size_t size;
    priskv_object_field *fields;
    int nfields;
};

struct priskv_object_field {
    priskv_object_field_type type;
    const char *name;
    size_t offset;
    size_t noffset;
    priskv_object *obj;
    bool _required;
    bool _ignored;
};

extern priskv_object priskv_boolean_obj;
extern priskv_object priskv_int_obj;
extern priskv_object priskv_uint64_obj;
extern priskv_object priskv_string_obj;

#define required true
#define optional false

#define ignored true
#define forced false

#define PRISKV_DECL_OBJECT_BEGIN(__name) static priskv_object_field __name##_fields[] = {

#define PRISKV_DECL_OBJECT_END(__name, __type)                                                       \
    }                                                                                              \
    ;                                                                                              \
    priskv_object __name##_obj = {                                                                   \
        .type = priskv_obj_type_object,                                                              \
        .size = sizeof(__type),                                                                    \
        .fields = __name##_fields,                                                                 \
        .nfields = sizeof(__name##_fields) / sizeof(priskv_object_field),                            \
    };

#define PRISKV_DECL_OBJECT_VALUE_FIELD(__type, __name, __field, __obj, __required, __ignored)        \
    {priskv_obj_field_type_value,                                                                    \
     __name,                                                                                       \
     offsetof(__type, __field),                                                                    \
     0,                                                                                            \
     &__obj##_obj,                                                                                 \
     __required,                                                                                   \
     __ignored},

#define PRISKV_DECL_OBJECT_ARRAY_FIELD(__type, __name, __field, __nfield, __obj, __required,         \
                                     __ignored)                                                    \
    {priskv_obj_field_type_array,                                                                    \
     __name,                                                                                       \
     offsetof(__type, __field),                                                                    \
     offsetof(__type, __nfield),                                                                   \
     &__obj##_obj,                                                                                 \
     __required,                                                                                   \
     __ignored},

typedef void *priskv_struct_ptr;

priskv_codec *priskv_codec_new();
void priskv_codec_destroy(priskv_codec *codec);
const char *priskv_codec_get_error(priskv_codec *codec);

char *priskv_codec_code(priskv_codec *codec, priskv_struct_ptr data, const priskv_object *obj);
priskv_struct_ptr priskv_codec_decode(priskv_codec *codec, const char *str, const priskv_object *obj);
int priskv_codec_free_struct(priskv_codec *codec, priskv_struct_ptr data, const priskv_object *obj);

priskv_object *priskv_codec_object_new(void);
void priskv_codec_object_append_field(priskv_object *object, const char *name, priskv_object *obj,
                                    bool __required, bool __ignored);
void priskv_codec_object_free(priskv_object *obj);

#endif /* __PRISKV_CODEC_H__ */
