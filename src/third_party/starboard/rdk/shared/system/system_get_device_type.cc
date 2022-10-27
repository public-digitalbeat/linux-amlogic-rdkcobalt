// Copyright 2016 The Cobalt Authors. All Rights Reserved.
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
#include <cstdlib>
#include "starboard/string.h"
#include "starboard/common/string.h"
#include "starboard/system.h"
#include "aml_device_property.h"


SbSystemDeviceType SbSystemGetDeviceType() {
  SbSystemDeviceType ret = kSbSystemDeviceTypeUnknown;
  char out_value[20];
  int value_length = 20;
  if (AmlDeviceGetProperty("YOUTUBE_DEVICE_TYPE",
        out_value, value_length) != AMLDEVICE_SUCCESS) {
    return ret;
  }

  if (!SbStringCompareNoCase(out_value, "BDP"))
    ret = kSbSystemDeviceTypeBlueRayDiskPlayer;
  else if (!SbStringCompareNoCase(out_value, "GAME"))
    ret = kSbSystemDeviceTypeGameConsole;
  else if (!SbStringCompareNoCase(out_value, "OTT"))
    ret = kSbSystemDeviceTypeOverTheTopBox;
  else if (!SbStringCompareNoCase(out_value, "STB"))
    ret = kSbSystemDeviceTypeSetTopBox;
  else if (!SbStringCompareNoCase(out_value, "TV"))
    ret = kSbSystemDeviceTypeTV;
  else if (!SbStringCompareNoCase(out_value, "DPC"))
    ret = kSbSystemDeviceTypeDesktopPC;
  else if (!SbStringCompareNoCase(out_value, "ATV"))
    ret = kSbSystemDeviceTypeAndroidTV;
  return ret;
}
