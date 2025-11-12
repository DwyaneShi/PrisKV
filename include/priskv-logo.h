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

#ifndef __PRISKV_LOGO__
#define __PRISKV_LOGO__

#if defined(__cplusplus)
extern "C"
{
#endif

#include <stdlib.h>

#define IMPORT_BIN(sect, file, sym)                                                                \
    asm(".section " #sect "\n"                                                                     \
        ".global " #sym "\n"                                                                       \
        ".global " #sym "_end\n" #sym ":\n"                                                        \
        ".incbin \"" file "\"\n" #sym "_end:\n"                                                    \
        ".section \".text\"\n")

/* Build help.html into atop binary */
IMPORT_BIN(".rodata", "priskv-logo.txt", priskv_logo);
extern char priskv_logo[], priskv_logo_end[];

static inline void priskv_show_logo()
{
    /* one more byte for '\0' */
    char *logo = (char *)calloc(1, priskv_logo_end - priskv_logo + 1);

    memcpy(logo, priskv_logo, priskv_logo_end - priskv_logo);
    printf("%s", logo);
    free(logo);
}

static inline void priskv_show_license()
{
    printf("\t - Copyright (c) 2025 ByteDance Ltd. and/or its affiliates"
           "\n\n");
}

#if defined(__cplusplus)
}
#endif

#endif /* __PRISKV_LOGO__ */
