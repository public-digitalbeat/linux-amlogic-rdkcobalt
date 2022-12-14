// Copyright 2020 The Cobalt Authors. All Rights Reserved.
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

#include "third_party/starboard/rdk/shared/system/extension_graphics.h"

#include "cobalt/extension/graphics.h"
#include "starboard/common/configuration_defaults.h"

namespace third_party {
namespace starboard {
namespace rdk {
namespace shared {
namespace system {

namespace {

float CobaltGetMaximumFrameIntervalInMilliseconds() {
  return -1.0f;
}

float CobaltGetMinimumFrameIntervalInMilliseconds() {
  return -1.0f;
}

bool CobaltIsMapToMeshEnabled() {
  return false;
}

const CobaltExtensionGraphicsApi kGraphicsApi = {
    kCobaltExtensionGraphicsName,
    3,
    &CobaltGetMaximumFrameIntervalInMilliseconds,
    &CobaltGetMinimumFrameIntervalInMilliseconds,
    &CobaltIsMapToMeshEnabled,
};

}  // namespace

const void* GetGraphicsApi() {
  return &kGraphicsApi;
}

}  // namespace system
}  // namespace shared
}  // namespace rdk
}  // namespace starboard
}  // third_party
