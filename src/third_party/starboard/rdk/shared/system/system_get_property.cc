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

#include <string.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <unistd.h>

#include "starboard/common/log.h"
#include "starboard/common/string.h"
#include "starboard/character.h"
#include "starboard/file.h"

#include "third_party/starboard/rdk/shared/rdkservices.h"
#include "third_party/starboard/rdk/shared/log_override.h"

#include "aml_device_property.h"

#if SB_API_VERSION >= 11
const char kCertificationScope[] = "amlogic-2021-amlogictvref"; // Please fill in the certification scope you get from google team
const char kBase64EncodedCertificationSecret[] = "Fake Secret"; // If you want to test with SW device authentication, please fill in the device secret key you get from google team
#endif

namespace {

bool CopyStringAndTestIfSuccess(char* out_value,
                                int value_length,
                                const char* from_value) {
  if (strlen(from_value) + 1 > value_length)
    return false;
  starboard::strlcpy(out_value, from_value, value_length);
  return true;
}

bool GetBrankName(char* out_value, int value_length) {
  if (AmlDeviceGetProperty("BRAND_NAME",
        out_value, value_length) == AMLDEVICE_SUCCESS) {
    return true;
  }
  return CopyStringAndTestIfSuccess(out_value, value_length, SB_PLATFORM_BRAND_NAME);
}

bool GetModelName(char* out_value, int value_length) {
  if (AmlDeviceGetProperty("MODEL_NAME",
        out_value, value_length) == AMLDEVICE_SUCCESS) {
    return true;
  }
  return CopyStringAndTestIfSuccess(out_value, value_length, SB_PLATFORM_MODEL_NAME);
}

bool GetOperatorName(char* out_value, int value_length) {
  if (AmlDeviceGetProperty("OPERATOR_NAME",
        out_value, value_length) == AMLDEVICE_SUCCESS) {
    return true;
  }
  return CopyStringAndTestIfSuccess(out_value, value_length, SB_PLATFORM_OPERATOR_NAME);
}

bool GetChipsetModelNumber(char* out_value, int value_length) {
  if (AmlDeviceGetProperty("CHIPSET_MODEL_NUM",
        out_value, value_length) == AMLDEVICE_SUCCESS) {
    return true;
  }
  return CopyStringAndTestIfSuccess(out_value, value_length, SB_PLATFORM_CHIPSET_MODEL_NUMBER_STRING);
}

bool GetFirmwareVersion(char* out_value, int value_length) {
  if (AmlDeviceGetProperty("FIRMWARE_VERSION",
        out_value, value_length) == AMLDEVICE_SUCCESS) {
    return true;
  }
  return CopyStringAndTestIfSuccess(out_value, value_length, SB_PLATFORM_FIRMWARE_VERSION_STRING);
}

bool GetSysIntegrateName(char* out_value, int value_length) {
  if (AmlDeviceGetProperty("SYSINTEGRATE_NAME",
        out_value, value_length) == AMLDEVICE_SUCCESS) {
    return true;
  }
  return CopyStringAndTestIfSuccess(out_value, value_length, SB_PLATFORM_SYSINTEGRATE_NAME);
}

bool GetModelYear(char* out_value, int value_length) {
  if (AmlDeviceGetProperty("MODEL_YEAR",
        out_value, value_length) == AMLDEVICE_SUCCESS) {
    return true;
  }
  return CopyStringAndTestIfSuccess(out_value, value_length, SB_PLATFORM_MODEL_YEAR_STRING);
}

bool GetFriendlyName(char* out_value, int value_length) {
  if (AmlDeviceGetProperty("FRIENDLY_NAME",
        out_value, value_length) == AMLDEVICE_SUCCESS) {
    return true;
  }
  return CopyStringAndTestIfSuccess(out_value, value_length, SB_PLATFORM_FRIENDLY_NAME);
}

bool GetPlatformName(char* out_value, int value_length) {
  if (AmlDeviceGetProperty("PLATFORM_NAME",
        out_value, value_length) == AMLDEVICE_SUCCESS) {
    return true;
  }
  return CopyStringAndTestIfSuccess(out_value, value_length, SB_PLATFORM_NAME);
}

#if SB_API_VERSION >= 11
bool GetCertificationScope(char* out_value, int value_length) {
  if (AmlDeviceGetProperty("CERT_SCOPE",
        out_value, value_length) == AMLDEVICE_SUCCESS) {
    return true;
  }
  return CopyStringAndTestIfSuccess(out_value, value_length, kCertificationScope);
}

bool GetCertificationSecret(char* out_value, int value_length) {
  if (AmlDeviceGetProperty("CERT_SECRET",
        out_value, value_length) == AMLDEVICE_SUCCESS) {
    return true;
  }
  if (kBase64EncodedCertificationSecret[0] == '\0')
        return false;
  return CopyStringAndTestIfSuccess(out_value, value_length,
      kBase64EncodedCertificationSecret);
}

#endif

}  // namespace

bool SbSystemGetProperty(SbSystemPropertyId property_id,
                         char* out_value,
                         int value_length) {
  if (!out_value || !value_length) {
    return false;
  }

  SB_LOG(INFO) << "property_id = " << property_id;

  switch (property_id) {
    case kSbSystemPropertyBrandName:
      return GetBrankName(out_value, value_length);
    case kSbSystemPropertyChipsetModelNumber:
      return GetChipsetModelNumber(out_value, value_length);
    case kSbSystemPropertyFirmwareVersion:
      return GetFirmwareVersion(out_value, value_length);
    case kSbSystemPropertyModelName:
      return GetModelName(out_value, value_length);

#if SB_API_VERSION >= 12
    case kSbSystemPropertySystemIntegratorName:
      return GetSysIntegrateName(out_value, value_length);
#elif SB_API_VERSION == 11
    case kSbSystemPropertyOriginalDesignManufacturerName:
      return GetSysIntegrateName(out_value, value_length);
#else
    case kSbSystemPropertyNetworkOperatorName:
#endif
    case kSbSystemPropertySpeechApiKey:
      return false;

    case kSbSystemPropertyModelYear:
      return GetModelYear(out_value, value_length);

    case kSbSystemPropertyFriendlyName:
      return GetFriendlyName(out_value, value_length);

    case kSbSystemPropertyPlatformName:
      return GetPlatformName(out_value, value_length);

#if SB_API_VERSION >= 11
    case kSbSystemPropertyCertificationScope:
      return GetCertificationScope(out_value, value_length);

#if SB_API_VERSION < 13
    case kSbSystemPropertyBase64EncodedCertificationSecret:
      return GetCertificationSecret(out_value, value_length);
#endif

#endif  // SB_API_VERSION >= 11

    default:
      SB_DLOG(WARNING) << __FUNCTION__
                       << ": Unrecognized property: " << property_id;
      break;
  }

  return false;
}

