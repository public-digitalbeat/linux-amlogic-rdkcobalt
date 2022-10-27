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
//
// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/configuration.h"
#include "starboard/configuration_constants.h"
#include "starboard/media.h"
#include "starboard/common/string.h"
#include "starboard/common/log.h"
#include "starboard/shared/starboard/media/media_support_internal.h"
#include "third_party/starboard/rdk/shared/media/gst_media_utils.h"
#include "aml_device_property.h"

static bool device_is_support_dolby = true; // Whether Cobalt should support AC3 and EAC3

static bool TryGetSupportDobly(void) {
  char out_value[20];
  int value_length = 20;

  if (AmlDeviceGetProperty("ENABLE_DOLBY",
        out_value, value_length) == AMLDEVICE_SUCCESS) {
    if ((SbStringCompareNoCaseN(out_value, "FALSE", 5) == 0
          || SbStringCompareNoCaseN(out_value, "false", 5) == 0))
      return false;
  }
  return true;
}

static bool SbSystemIsSupportDobly(void) {
  static bool read_dolby_flag = false;
  if (false == read_dolby_flag) {
    device_is_support_dolby = TryGetSupportDobly();
    read_dolby_flag = true;
  }
  return device_is_support_dolby;
}

bool SbMediaIsAudioSupported(SbMediaAudioCodec audio_codec,
                             const char* content_type,
                             int64_t bitrate) {
    if (kSbMediaAudioCodecAc3 == audio_codec ||
        kSbMediaAudioCodecEac3 == audio_codec) {
      if (!SbSystemIsSupportDobly())
        return false;
    }
    return bitrate < kSbMediaMaxAudioBitrateInBitsPerSecond &&
         third_party::starboard::rdk::shared::media::
             GstRegistryHasElementForMediaType(audio_codec);
}
