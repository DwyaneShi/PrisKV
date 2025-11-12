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

#include "jsonobjs.h"

/* define for priskvClusterMetaDataNodeSlotRange_obj */
PRISKV_DECL_OBJECT_BEGIN(priskvClusterMetaDataNodeSlotRange)
PRISKV_DECL_OBJECT_VALUE_FIELD(priskvClusterMetaDataNodeSlotRange, "start", start, priskv_int, required,
                             forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(priskvClusterMetaDataNodeSlotRange, "end", end, priskv_int, required,
                             forced)
PRISKV_DECL_OBJECT_END(priskvClusterMetaDataNodeSlotRange, priskvClusterMetaDataNodeSlotRange)

/* define for priskvClusterMetaDataNodeInfo_obj */
PRISKV_DECL_OBJECT_BEGIN(priskvClusterMetaDataNodeInfo)
PRISKV_DECL_OBJECT_VALUE_FIELD(priskvClusterMetaDataNodeInfo, "name", name, priskv_string, required,
                             forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(priskvClusterMetaDataNodeInfo, "addr", addr, priskv_string, required,
                             forced)
PRISKV_DECL_OBJECT_VALUE_FIELD(priskvClusterMetaDataNodeInfo, "port", port, priskv_int, required, ignored)
PRISKV_DECL_OBJECT_ARRAY_FIELD(priskvClusterMetaDataNodeInfo, "slots", slotRanges, slotRangeCount,
                             priskvClusterMetaDataNodeSlotRange, required, ignored)
PRISKV_DECL_OBJECT_END(priskvClusterMetaDataNodeInfo, priskvClusterMetaDataNodeInfo)

/* define for priskvClusterMetaDataInfo_obj */
PRISKV_DECL_OBJECT_BEGIN(priskvClusterMetaDataInfo)
PRISKV_DECL_OBJECT_VALUE_FIELD(priskvClusterMetaDataInfo, "version", version, priskv_int, required,
                             forced)
PRISKV_DECL_OBJECT_ARRAY_FIELD(priskvClusterMetaDataInfo, "nodes", nodes, nodeCount,
                             priskvClusterMetaDataNodeInfo, required, forced)
PRISKV_DECL_OBJECT_END(priskvClusterMetaDataInfo, priskvClusterMetaDataInfo)
