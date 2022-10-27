//
// Copyright 2020 Comcast Cable Communications Management, LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef THIRD_PARTY_STARBOARD_RDK_SHARED_LIBCOBALT_H_
#define THIRD_PARTY_STARBOARD_RDK_SHARED_LIBCOBALT_H_

#include "starboard/export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  COBALT_PLUGIN_NOTIF_FREEZE_DONE = 0, // for suspend done
} CobaltPluginNotifyReason_e;

typedef  void (*libCobaltCallback)(int reason, void *param);

SB_EXPORT_PLATFORM void SbRdkHandleDeepLink(const char* link);
SB_EXPORT_PLATFORM void SbRdkSuspend();
SB_EXPORT_PLATFORM void SbRdkResume();
SB_EXPORT_PLATFORM void SbRdkPause();
SB_EXPORT_PLATFORM void SbRdkUnpause();
SB_EXPORT_PLATFORM void SbRdkQuit();
SB_EXPORT_PLATFORM void SbRdkSetSetting(const char* key, const char* json);
SB_EXPORT_PLATFORM int  SbRdkGetSetting(const char* key, char** out_json);  // caller is responsible to free
SB_EXPORT_PLATFORM void SbRdkRegisterNotify(libCobaltCallback callback); // Register callback

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // THIRD_PARTY_STARBOARD_RDK_SHARED_LIBCOBALT_H_
