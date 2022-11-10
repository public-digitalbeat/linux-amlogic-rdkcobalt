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

#ifndef THIRD_PARTY_STARBOARD_RDK_SHARED_RDKSERVICES_H_
#define THIRD_PARTY_STARBOARD_RDK_SHARED_RDKSERVICES_H_

#include "starboard/configuration.h"
#include "starboard/common/scoped_ptr.h"

#include <string>

#if defined(VIDEO_RESOLUTION_1080P)
  #define COBALT_VIDEO_RESOLUTION_WIDTH  1920
  #define COBALT_VIDEO_RESOLUTION_HEIGHT 1080
#elif defined(VIDEO_RESOLUTION_2160P)
  #define COBALT_VIDEO_RESOLUTION_WIDTH  3840
  #define COBALT_VIDEO_RESOLUTION_HEIGHT 2160
#elif defined(VIDEO_RESOLUTION_720P)
  #define COBALT_VIDEO_RESOLUTION_WIDTH  1280
  #define COBALT_VIDEO_RESOLUTION_HEIGHT 720
#else
  #Error (Now we just support UHD, FHD, and HD)
#endif


namespace third_party {
namespace starboard {
namespace rdk {
namespace shared {

struct ResolutionInfo {
  ResolutionInfo() {}
  ResolutionInfo(uint32_t w, uint32_t h)
    : Width(w), Height(h) {}
  uint32_t Width { COBALT_VIDEO_RESOLUTION_WIDTH };
  uint32_t Height { COBALT_VIDEO_RESOLUTION_HEIGHT };
};

class DisplayInfo {
public:
  DisplayInfo();
  ~DisplayInfo();
  ResolutionInfo GetResolution() const;
  float GetDiagonalSizeInInches() const;
  bool HasHDRSupport() const;
private:
  struct Impl;
  mutable ::starboard::scoped_ptr<Impl> impl_;
};

class DeviceIdentification {
public:
  static std::string GetChipset();
  static std::string GetFirmwareVersion();
};

class NetworkInfo {
public:
  NetworkInfo();
  ~NetworkInfo();
  static bool IsConnectionTypeWireless();
private:
  struct NetworkInfoImpl;
  mutable ::starboard::scoped_ptr<NetworkInfoImpl> impl_;

};

class RDKShellInfo {
public:
  RDKShellInfo();
  ~RDKShellInfo();
  bool ImpGetFocusStatus();
  std::string ImpGetFocusAppName();
private:
  struct RDKShellInfoImpl;
  mutable ::starboard::scoped_ptr<RDKShellInfoImpl> impl_;
};

class TextToSpeech {
public:
  static bool IsEnabled();
  static void Speak(const std::string& text);
  static void Cancel();
};
class HdcpProfile{

public:
  HdcpProfile();
  ~HdcpProfile();
static bool GetConnectstatus();
private:
  struct HdcpProfileImpl;
  mutable ::starboard::scoped_ptr<HdcpProfileImpl> impl_;
};

class VoiceInput {
public:
  VoiceInput();
  ~VoiceInput();

  static int GetData(void *data, int size);
  static bool StartRecord();
  static bool StopRecord();
  static bool isSampleRateSupport(int sampleRate);
  static bool isMuted();
  static int GetSampleRate();
  static int GetMicroPhoneEnable();

private:
  struct VoiceInputImpl;
  mutable ::starboard::scoped_ptr<VoiceInputImpl> impl_;
  static VoiceInput *_instance;
};

}  // namespace shared
}  // namespace rdk
}  // namespace starboard
}  // namespace third_party

#endif  // THIRD_PARTY_STARBOARD_RDK_SHARED_RDKSERVICES_H_
