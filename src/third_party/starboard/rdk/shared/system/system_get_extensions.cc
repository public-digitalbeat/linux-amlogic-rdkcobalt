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
//
// Copyright 2019 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/system.h"

#include "cobalt/extension/configuration.h"
#include "cobalt/extension/crash_handler.h"
#include "cobalt/extension/graphics.h"
#include "starboard/common/string.h"
#include "starboard/shared/starboard/crash_handler.h"
#if SB_IS(EVERGREEN_COMPATIBLE)
#include "starboard/elf_loader/evergreen_config.h"
#endif
#include "third_party/starboard/rdk/shared/configuration.h"
#include "third_party/starboard/rdk/shared/system/extension_graphics.h"

const void* SbSystemGetExtension(const char* name) {
  if (SbStringCompareNoCase(name, kCobaltExtensionConfigurationName) == 0) {
    return third_party::starboard::rdk::shared::GetConfigurationApi();
  }
#if SB_IS(EVERGREEN_COMPATIBLE)
  const starboard::elf_loader::EvergreenConfig* evergreen_config =
      starboard::elf_loader::EvergreenConfig::GetInstance();
  if (evergreen_config != NULL &&
      evergreen_config->custom_get_extension_ != NULL) {
    const void* ext = evergreen_config->custom_get_extension_(name);
    if (ext != NULL) {
      return ext;
    }
  }
#endif

#if 0
  // For extension configuration
  if (SbStringCompareNoCase(name, kCobaltExtensionConfigurationName) == 0) {
    return starboard::stub::GetConfigurationApi();
  }
#endif

  // For extension graphics
  if (SbStringCompareNoCase(name, kCobaltExtensionGraphicsName) == 0) {
    return third_party::starboard::rdk::shared::system::GetGraphicsApi();
  }

  // For extersion crash handler
  if (SbStringCompareNoCase(name, kCobaltExtensionCrashHandlerName) == 0) {
    return starboard::common::GetCrashHandlerApi();
  }

  return NULL;
}

