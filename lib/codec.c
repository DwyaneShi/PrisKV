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

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>

#include "priskv-codec.h"

#define MAX_STRING_SIZE 255

struct priskv_codec {
    char error_string[MAX_STRING_SIZE + 1];
};

priskv_codec *priskv_codec_new()
{
    priskv_codec *codec = malloc(sizeof(priskv_codec));
    if (!codec) {
        return NULL;
    }

    memset(codec, 0, sizeof(priskv_codec));

    return codec;
}

void priskv_codec_destroy(priskv_codec *codec)
{
    free(codec);
}

static void __priskv_codec_set_error(priskv_codec *codec, const char *error_string, ...)
{
    va_list ap;
    char errstr[MAX_STRING_SIZE + 1] = {0};

    if (!codec) {
        return;
    }

    va_start(ap, error_string);
    if (vsnprintf(errstr, MAX_STRING_SIZE, error_string, ap) < 0) {
        strncpy(errstr, "could not format error string!", MAX_STRING_SIZE);
    }
    va_end(ap);

    strncpy(codec->error_string, errstr, MAX_STRING_SIZE + 1);
}

const char *priskv_codec_get_error(priskv_codec *codec)
{
    return codec ? codec->error_string : "";
}

priskv_object priskv_boolean_obj = {
    .type = priskv_obj_type_boolean,
    .size = sizeof(bool),
    .fields = NULL,
    .nfields = 0,
};

priskv_object priskv_int_obj = {
    .type = priskv_obj_type_int,
    .size = sizeof(int),
    .fields = NULL,
    .nfields = 0,
};

priskv_object priskv_uint64_obj = {
    .type = priskv_obj_type_uint64,
    .size = sizeof(uint64_t),
    .fields = NULL,
    .nfields = 0,
};

priskv_object priskv_string_obj = {
    .type = priskv_obj_type_string,
    .size = sizeof(char *),
    .fields = NULL,
    .nfields = 0,
};

static int __priskv_decode_obj(priskv_codec *codec, json_object *jobj, void *data,
                             const priskv_object *obj);
static int __priskv_free_struct(priskv_codec *codec, void *data, const priskv_object *obj);
static struct json_object *__priskv_code_obj(priskv_codec *codec, void *data, const priskv_object *obj);

static int __priskv_decode_boolean(priskv_codec *codec, struct json_object *jobj, void *data,
                                 const priskv_object *obj)
{
    if (json_object_get_type(jobj) != json_type_boolean) {
        __priskv_codec_set_error(codec, "type is not boolean");
        return -1;
    }

    *(bool *)data = json_object_get_boolean(jobj);
    return 0;
}

static int __priskv_decode_int(priskv_codec *codec, struct json_object *jobj, void *data,
                             const priskv_object *obj)
{
    if (json_object_get_type(jobj) != json_type_int) {
        __priskv_codec_set_error(codec, "type is not int");
        return -1;
    }

    *(int *)data = json_object_get_int(jobj);
    return 0;
}

static int __priskv_decode_uint64(priskv_codec *codec, struct json_object *jobj, void *data,
                                const priskv_object *obj)
{
    if (json_object_get_type(jobj) != json_type_int) {
        __priskv_codec_set_error(codec, "type is not int");
        return -1;
    }

    *(uint64_t *)data = json_object_get_uint64(jobj);
    return 0;
}

static int __priskv_decode_string(priskv_codec *codec, struct json_object *jobj, void *data,
                                const priskv_object *obj)
{
    if (json_object_get_type(jobj) != json_type_string) {
        __priskv_codec_set_error(codec, "type is not string");
        return -1;
    }

    *(const char **)data = strdup(json_object_get_string(jobj));
    return 0;
}

static int __priskv_decode_object(priskv_codec *codec, struct json_object *jobj, void *data,
                                const priskv_object *obj)
{
    if (json_object_get_type(jobj) != json_type_object) {
        __priskv_codec_set_error(codec, "type is not object");
        return -1;
    }

    return __priskv_decode_obj(codec, jobj, data, obj);
}

typedef int (*decoder)(priskv_codec *codec, struct json_object *jobj, void *data,
                       const priskv_object *obj);
static decoder priskv_value_decode[] = {
    [priskv_obj_type_boolean] = __priskv_decode_boolean, [priskv_obj_type_int] = __priskv_decode_int,
    [priskv_obj_type_uint64] = __priskv_decode_uint64,   [priskv_obj_type_string] = __priskv_decode_string,
    [priskv_obj_type_object] = __priskv_decode_object,
};

typedef int (*decoder_clean)(priskv_codec *codec, void *data, const priskv_object *obj);
static decoder_clean priskv_value_decode_clean[];

static int __priskv_decode_obj(priskv_codec *codec, json_object *jobj, void *data,
                             const priskv_object *obj)
{
    struct priskv_object_field *field;
    struct json_object *field_obj;
    priskv_object_type obj_type;
    void *field_data;
    int i;

    if (!jobj) {
        __priskv_codec_set_error(codec, "json object is null");
        goto fail;
    }

    if (!data) {
        __priskv_codec_set_error(codec, "data is null");
        goto fail;
    }

    if (!obj) {
        __priskv_codec_set_error(codec, "priskv object is null");
        goto fail;
    }

    for (i = 0; i < obj->nfields; i++) {
        field = &obj->fields[i];
        field_data = data + field->offset;
        field_obj = json_object_object_get(jobj, field->name);
        obj_type = field->obj->type;

        if (!field_obj) {
            if (field->_required) {
                __priskv_codec_set_error(codec, "not found `%s` that is required", field->name);
                goto fail;
            }
            continue;
        }

        if (obj_type >= priskv_obj_type_max) {
            __priskv_codec_set_error(codec, "unknown object type [%d] for `%s`", obj_type,
                                   field->name);
            goto fail;
        }

        switch (field->type) {
        case priskv_obj_field_type_value:
            if (priskv_value_decode[obj_type](codec, field_obj, field_data, field->obj) < 0) {
                __priskv_codec_set_error(codec, "failed to decode `%s`: %s", field->name,
                                       priskv_codec_get_error(codec));
                goto fail;
            }
            break;
        case priskv_obj_field_type_array: {
            if (json_object_get_type(field_obj) != json_type_array) {
                __priskv_codec_set_error(codec, "failed to decode `%s`: type is not array",
                                       field->name);
                goto fail;
            }

            size_t len = json_object_array_length(field_obj);
            *(size_t *)(data + field->noffset) = len;
            *(void **)field_data = calloc(len, field->obj->size);
            struct json_object *field_ele_obj;
            void *field_ele_data;

            for (size_t j = 0; j < len; j++) {
                field_ele_obj = json_object_array_get_idx(field_obj, j);
                field_ele_data = *(void **)field_data + field->obj->size * j;
                if (priskv_value_decode[obj_type](codec, field_ele_obj, field_ele_data, field->obj) <
                    0) {
                    __priskv_codec_set_error(codec, "failed to decode array `%s`: %s", field->name,
                                           priskv_codec_get_error(codec));
                    free(*(void **)field_data);
                    goto fail;
                }
            }
        } break;
        default:
            __priskv_codec_set_error(codec, "unknown field type [%d]", field->type);
            goto fail;
            break;
        }
    }

    return 0;

fail:
    return -1;
}

static int __priskv_decode_clean(priskv_codec *codec, void *data, const priskv_object *obj)
{
    return 0;
}

static int __priskv_decode_string_clean(priskv_codec *codec, void *data, const priskv_object *obj)
{
    free(*(void **)data);
    return 0;
}

static int __priskv_decode_object_clean(priskv_codec *codec, void *data, const priskv_object *obj)
{
    return __priskv_free_struct(codec, data, obj);
}

typedef int (*decoder_clean)(priskv_codec *codec, void *data, const priskv_object *obj);
static decoder_clean priskv_value_decode_clean[] = {
    [priskv_obj_type_boolean] = __priskv_decode_clean,
    [priskv_obj_type_int] = __priskv_decode_clean,
    [priskv_obj_type_uint64] = __priskv_decode_clean,
    [priskv_obj_type_string] = __priskv_decode_string_clean,
    [priskv_obj_type_object] = __priskv_decode_object_clean,
};

static int __priskv_free_struct(priskv_codec *codec, void *data, const priskv_object *obj)
{
    struct priskv_object_field *field;
    priskv_object_type obj_type;
    void *field_data;
    int i, ret;

    if (!data) {
        __priskv_codec_set_error(codec, "data is null");
        goto fail;
    }

    if (!obj) {
        __priskv_codec_set_error(codec, "priskv object is null");
        goto fail;
    }

    for (i = 0; i < obj->nfields; i++) {
        field = &obj->fields[i];
        field_data = data + field->offset;
        obj_type = field->obj->type;

        if (obj_type >= priskv_obj_type_max) {
            __priskv_codec_set_error(codec, "unknown object type [%d] for `%s`", obj_type,
                                   field->name);
            goto fail;
        }

        switch (field->type) {
        case priskv_obj_field_type_value:
            ret = priskv_value_decode_clean[obj_type](codec, field_data, field->obj);
            if (ret) {
                __priskv_codec_set_error(codec, "failed to clean `%s`: %s", field->name,
                                       priskv_codec_get_error(codec));
                goto fail;
            }
            break;
        case priskv_obj_field_type_array: {
            size_t len = *(size_t *)(data + field->noffset);
            for (size_t j = 0; j < len; j++) {
                void *field_ele_data = *(void **)field_data + field->obj->size * j;
                ret = priskv_value_decode_clean[obj_type](codec, field_ele_data, field->obj);
                if (ret) {
                    __priskv_codec_set_error(codec, "failed to clean array element `%s`: %s",
                                           field->name, priskv_codec_get_error(codec));
                    goto fail;
                }
            }
            free(*(void **)field_data);
        } break;
        default:
            break;
        }
    }

    return 0;

fail:
    return -1;
}

static struct json_object *__priskv_code_boolean(priskv_codec *codec, void *data,
                                               const priskv_object *obj)
{
    return json_object_new_boolean(*(bool *)data);
}

static struct json_object *__priskv_code_int(priskv_codec *codec, void *data, const priskv_object *obj)
{
    return json_object_new_int(*(int *)data);
}

static struct json_object *__priskv_code_uint64(priskv_codec *codec, void *data, const priskv_object *obj)
{
    return json_object_new_uint64(*(uint64_t *)data);
}

static struct json_object *__priskv_code_string(priskv_codec *codec, void *data, const priskv_object *obj)
{
    return !(*(void **)data) ? json_object_new_string("") : json_object_new_string(*(void **)data);
}

static struct json_object *__priskv_code_object(priskv_codec *codec, void *data, const priskv_object *obj)
{
    return __priskv_code_obj(codec, data, obj);
}

typedef struct json_object *(*coder)(priskv_codec *codec, void *data, const priskv_object *obj);
static coder priskv_value_code[] = {
    [priskv_obj_type_boolean] = __priskv_code_boolean, [priskv_obj_type_int] = __priskv_code_int,
    [priskv_obj_type_uint64] = __priskv_code_uint64,   [priskv_obj_type_string] = __priskv_code_string,
    [priskv_obj_type_object] = __priskv_code_object,
};

static bool __priskv_code_boolean_ignored(priskv_codec *codec, void *data, const priskv_object *obj)
{
    return false;
}

static bool __priskv_code_int_ignored(priskv_codec *codec, void *data, const priskv_object *obj)
{
    return !(*(int *)data);
}

static bool __priskv_code_uint64_ignored(priskv_codec *codec, void *data, const priskv_object *obj)
{
    return !(*(uint64_t *)data);
}

static bool __priskv_code_string_ignored(priskv_codec *codec, void *data, const priskv_object *obj)
{
    return !(*(void **)data);
}

static bool __priskv_code_object_ignored(priskv_codec *codec, void *data, const priskv_object *obj)
{
    return false;
}

typedef bool (*is_ignored)(priskv_codec *codec, void *data, const priskv_object *obj);
static is_ignored priskv_value_is_ignored[] = {
    [priskv_obj_type_boolean] = __priskv_code_boolean_ignored,
    [priskv_obj_type_int] = __priskv_code_int_ignored,
    [priskv_obj_type_uint64] = __priskv_code_uint64_ignored,
    [priskv_obj_type_string] = __priskv_code_string_ignored,
    [priskv_obj_type_object] = __priskv_code_object_ignored,
};

static struct json_object *__priskv_code_obj(priskv_codec *codec, void *data, const priskv_object *obj)
{
    struct priskv_object_field *field;
    struct json_object *jobj = NULL, *field_obj = NULL;
    priskv_object_type obj_type;
    void *field_data;
    int i;

    if (!data) {
        __priskv_codec_set_error(codec, "data is null");
        goto fail;
    }

    if (!obj) {
        __priskv_codec_set_error(codec, "priskv object is null");
        goto fail;
    }

    jobj = json_object_new_object();
    if (!jobj) {
        __priskv_codec_set_error(codec, "failed to new json object: %s", strerror(errno));
        goto fail;
    }

    for (i = 0; i < obj->nfields; i++) {
        field = &obj->fields[i];
        field_data = data + field->offset;
        obj_type = field->obj->type;

        if (obj_type >= priskv_obj_type_max) {
            __priskv_codec_set_error(codec, "unknown object type [%d] for `%s`", obj_type,
                                   field->name);
            goto fail;
        }

        switch (field->type) {
        case priskv_obj_field_type_value:
            if (field->_ignored && priskv_value_is_ignored[obj_type](codec, field_data, field->obj)) {
                continue;
            }

            field_obj = priskv_value_code[obj_type](codec, field_data, field->obj);
            if (!field_obj) {
                __priskv_codec_set_error(codec, "failed to code `%s`", field->name);
                goto fail;
            }
            break;
        case priskv_obj_field_type_array: {
            struct json_object *field_ele_obj;
            void *field_ele_data;
            int len = *(int *)(data + field->noffset);

            if (field->_ignored && len == 0) {
                continue;
            }

            field_obj = json_object_new_array_ext(len);
            if (!field_obj) {
                __priskv_codec_set_error(codec, "failed to new json array object for `%s`: %s",
                                       field->name, strerror(errno));
                goto fail;
            }

            for (size_t j = 0; j < len; j++) {
                field_ele_data = *(void **)field_data + field->obj->size * j;
                field_ele_obj = priskv_value_code[obj_type](codec, field_ele_data, field->obj);
                if (!field_ele_obj) {
                    __priskv_codec_set_error(codec, "failed to code array element for `%s`",
                                           field->name);
                    json_object_put(field_obj);
                    goto fail;
                }

                if (json_object_array_add(field_obj, field_ele_obj) < 0) {
                    __priskv_codec_set_error(codec, "failed to add element[%lu] for `%s`", j,
                                           field->name);
                    json_object_put(field_obj);
                    goto fail;
                };
            }
        } break;
        default:
            __priskv_codec_set_error(codec, "unknown field type [%d]", field->type);
            goto fail;
            break;
        }

        if (json_object_object_add(jobj, field->name, field_obj) < 0) {
            __priskv_codec_set_error(codec, "failed to add json object `%s`", field->name);
            goto fail;
        }
    }

    return jobj;

fail:
    json_object_put(jobj);
    return NULL;
}

priskv_struct_ptr priskv_codec_decode(priskv_codec *codec, const char *str, const priskv_object *obj)
{
    struct json_object *jobj;
    enum json_tokener_error jerr;
    void *data;

    assert(codec != NULL);

    if (!str) {
        __priskv_codec_set_error(codec, "string is empty");
        return NULL;
    }

    if (!obj) {
        __priskv_codec_set_error(codec, "object is empty");
        return NULL;
    }

    jobj = json_tokener_parse_verbose(str, &jerr);
    if (!jobj) {
        __priskv_codec_set_error(codec, "failed to parse json: %s", json_tokener_error_desc(jerr));
        return NULL;
    }

    data = malloc(obj->size);
    memset(data, 0, obj->size);
    if (__priskv_decode_obj(codec, jobj, data, obj) < 0) {
        free(data);
        data = NULL;
    }

    json_object_put(jobj);

    return data;
}

int priskv_codec_free_struct(priskv_codec *codec, priskv_struct_ptr data, const priskv_object *obj)
{
    if (!data) {
        return 0;
    }

    assert(codec != NULL);

    if (__priskv_free_struct(codec, data, obj) < 0) {
        return -1;
    }

    free(data);
    return 0;
}

char *priskv_codec_code(priskv_codec *codec, priskv_struct_ptr data, const priskv_object *obj)
{
    struct json_object *jobj;
    char *str;

    assert(codec != NULL);

    if (!data) {
        __priskv_codec_set_error(codec, "struct is empty");
        return NULL;
    }

    if (!obj) {
        __priskv_codec_set_error(codec, "object is empty");
        return NULL;
    }

    jobj = __priskv_code_obj(codec, data, obj);
    if (!jobj) {
        return NULL;
    }

    str = strdup(json_object_to_json_string(jobj));

    json_object_put(jobj);

    return str;
}

priskv_object *priskv_codec_object_new(void)
{
    priskv_object *obj = calloc(1, sizeof(priskv_object));
    obj->type = priskv_obj_type_object;

    return obj;
}

void priskv_codec_object_append_field(priskv_object *object, const char *name, priskv_object *obj,
                                    bool __required, bool __ignored)
{
    object->nfields++;
    object->fields = realloc(object->fields, sizeof(struct priskv_object_field) * object->nfields);
    object->fields[object->nfields - 1].type = priskv_obj_field_type_value;
    object->fields[object->nfields - 1].name = name;
    object->fields[object->nfields - 1].offset = object->size;
    object->fields[object->nfields - 1].obj = obj;
    object->fields[object->nfields - 1]._required = __required;
    object->fields[object->nfields - 1]._ignored = __ignored;

    object->size += obj->size;
}

void priskv_codec_object_free(priskv_object *obj)
{
    if (!obj) {
        return;
    }

    if (obj->fields) {
        free(obj->fields);
    }

    free(obj);
}
