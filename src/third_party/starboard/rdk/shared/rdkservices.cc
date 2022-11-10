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

#include "third_party/starboard/rdk/shared/rdkservices.h"

#include <algorithm>
#include <string>

#include <websocket/JSONRPCLink.h>

#include <interfaces/json/JsonData_DeviceIdentification.h>
#include <interfaces/json/JsonData_HDRProperties.h>
#include <interfaces/json/JsonData_PlayerProperties.h>

#ifdef HAS_SECURITY_AGENT
#include <securityagent/securityagent.h>
#endif

#include "starboard/atomic.h"
#include "starboard/common/condition_variable.h"
#include "starboard/common/mutex.h"
#include "starboard/common/string.h"
#include "starboard/event.h"
#include "starboard/once.h"

#include "third_party/starboard/rdk/shared/application_rdk.h"
#include "third_party/starboard/rdk/shared/log_override.h"

#include "aml_device_property.h"

using namespace WPEFramework;

namespace third_party {
namespace starboard {
namespace rdk {
namespace shared {

namespace {

const uint32_t kDefaultTimeoutMs = 100;
const char kDisplayInfoCallsign[] = "DisplayInfo.1";
const char kPlayerInfoCallsign[] = "PlayerInfo.1";
const char kDeviceIdentificationCallsign[] = "DeviceIdentification.1";
const char kNetworkCallsign[] = "org.rdk.Network.1";
const char kTTSCallsign[] = "org.rdk.TextToSpeech.1";
const char kHdcpProfileCallsign[] = "org.rdk.HdcpProfile.1";
const char kRDKShellCallsign[] = "org.rdk.RDKShell.1";
const char kVoiceInputCallsign[] = "org.rdk.VoiceInput.1";

const uint32_t kPriviligedRequestErrorCode = -32604U;

class ServiceLink {
  ::starboard::scoped_ptr<JSONRPC::LinkType<Core::JSON::IElement>> link_;
  std::string callsign_;

#ifdef HAS_SECURITY_AGENT
  static Core::OptionalType<std::string> getToken() {
    if (getenv("THUNDER_SECURITY_OFF") != nullptr)
      return {};

    const uint32_t kMaxBufferSize = 2 * 1024;
    const std::string payload = "https://www.youtube.com";

    Core::OptionalType<std::string> token;
    std::vector<uint8_t> buffer;
    buffer.resize(kMaxBufferSize);

    for (int i = 0; i < 5; ++i) {
      uint32_t inputLen = std::min(kMaxBufferSize, (uint32_t)payload.length());
      ::memcpy(buffer.data(), payload.c_str(), inputLen);

      int outputLen = GetToken(kMaxBufferSize, inputLen, buffer.data());
      SB_DCHECK(outputLen != 0);

      if (outputLen > 0) {
        token = std::string(reinterpret_cast<const char *>(buffer.data()),
                            outputLen);
        break;
      } else if (outputLen < 0) {
        uint32_t rc = -outputLen;
        if (rc == Core::ERROR_TIMEDOUT && i < 5) {
          SB_LOG(ERROR) << "Failed to get token, trying again. rc = " << rc
                        << " ( " << Core::ErrorToString(rc) << " )";
          continue;
        }
        SB_LOG(ERROR) << "Failed to get token, give up. rc = " << rc << " ( "
                      << Core::ErrorToString(rc) << " )";
      }
      break;
    }
    return token;
  }
#endif

  static std::string buildQuery() {
    std::string query;
#ifdef HAS_SECURITY_AGENT
    static const auto token = getToken();
    if (token.IsSet() && !token.Value().empty())
      query = "token=" + token.Value();
#endif
    return query;
  }

  static bool enableEnvOverrides() {
    static bool enable_env_overrides = ([]() {
      std::string envValue;
      if ((Core::SystemInfo::GetEnvironment("COBALT_ENABLE_OVERRIDES",
                                            envValue) == true) &&
          (envValue.empty() == false)) {
        return envValue.compare("1") == 0 || envValue.compare("true") == 0;
      }
      return false;
    })();
    return enable_env_overrides;
  }

public:
  ServiceLink(const std::string callsign) : callsign_(callsign) {
    if (getenv("THUNDER_ACCESS") != nullptr)
      link_.reset(new JSONRPC::LinkType<Core::JSON::IElement>(
          callsign, nullptr, false, buildQuery()));
  }

  template <typename PARAMETERS>
  uint32_t Get(const uint32_t waitTime, const string &method,
               PARAMETERS &sendObject) {
    if (enableEnvOverrides()) {
      std::string envValue;
      std::string envName =
          Core::JSONRPC::Message::Callsign(callsign_) + "_" + method;
      envName.erase(std::remove(envName.begin(), envName.end(), '.'),
                    envName.end());
      if (Core::SystemInfo::GetEnvironment(envName, envValue) == true) {
        return sendObject.FromString(envValue) ? Core::ERROR_NONE
                                               : Core::ERROR_GENERAL;
      }
    }
    if (!link_)
      return Core::ERROR_UNAVAILABLE;
    return link_->template Get<PARAMETERS>(waitTime, method, sendObject);
  }

  template <typename PARAMETERS, typename HANDLER, typename REALOBJECT>
  uint32_t Dispatch(const uint32_t waitTime, const string &method,
                    const PARAMETERS &parameters, const HANDLER &callback,
                    REALOBJECT *objectPtr) {
    if (!link_)
      return Core::ERROR_UNAVAILABLE;
    return link_->template Dispatch<PARAMETERS, HANDLER, REALOBJECT>(
        waitTime, method, parameters, callback, objectPtr);
  }

  template <typename HANDLER, typename REALOBJECT>
  uint32_t Dispatch(const uint32_t waitTime, const string &method,
                    const HANDLER &callback, REALOBJECT *objectPtr) {
    if (!link_)
      return Core::ERROR_UNAVAILABLE;
    return link_->template Dispatch<void, HANDLER, REALOBJECT>(
        waitTime, method, callback, objectPtr);
  }

  template <typename INBOUND, typename METHOD, typename REALOBJECT>
  uint32_t Subscribe(const uint32_t waitTime, const string &eventName,
                     const METHOD &method, REALOBJECT *objectPtr) {
    if (!link_)
      return enableEnvOverrides() ? Core::ERROR_NONE : Core::ERROR_UNAVAILABLE;
    return link_->template Subscribe<INBOUND, METHOD, REALOBJECT>(
        waitTime, eventName, method, objectPtr);
  }

  void Unsubscribe(const uint32_t waitTime, const string &eventName) {
    if (!link_)
      return;
    return link_->Unsubscribe(waitTime, eventName);
  }
};

struct DeviceIdImpl {
  DeviceIdImpl() {
    JsonData::DeviceIdentification::DeviceidentificationData data;
    uint32_t rc = ServiceLink(kDeviceIdentificationCallsign)
                      .Get(2000, "deviceidentification", data);
    if (Core::ERROR_NONE == rc) {
      chipset = data.Chipset.Value();
      firmware_version = data.Firmwareversion.Value();
      std::replace(chipset.begin(), chipset.end(), ' ', '-');
    }
    if (Core::ERROR_NONE != rc) {
#if defined(SB_PLATFORM_CHIPSET_MODEL_NUMBER_STRING)
      chipset = SB_PLATFORM_CHIPSET_MODEL_NUMBER_STRING;
#endif
#if defined(SB_PLATFORM_FIRMWARE_VERSION_STRING)
      firmware_version = SB_PLATFORM_FIRMWARE_VERSION_STRING;
#endif
    }
  }
  std::string chipset;
  std::string firmware_version;
};

SB_ONCE_INITIALIZE_FUNCTION(DeviceIdImpl, GetDeviceIdImpl);

struct TextToSpeechImpl {
private:
  ::starboard::atomic_bool is_enabled_{false};
  int64_t speech_id_{-1};
  int32_t speech_request_num_{0};
  ServiceLink tts_link_{kTTSCallsign};
  ::starboard::Mutex mutex_;
  ::starboard::ConditionVariable condition_{mutex_};

  struct IsTTSEnabledInfo : public Core::JSON::Container {
    IsTTSEnabledInfo() : Core::JSON::Container() {
      Add(_T("isenabled"), &IsEnabled);
    }
    IsTTSEnabledInfo(const IsTTSEnabledInfo &) = delete;
    IsTTSEnabledInfo &operator=(const IsTTSEnabledInfo &) = delete;

    Core::JSON::Boolean IsEnabled;
  };

  struct SpeakResult : public Core::JSON::Container {
    SpeakResult() : Core::JSON::Container(), SpeechId(-1) {
      Add(_T("speechid"), &SpeechId);
    }
    SpeakResult(const SpeakResult &) = delete;
    SpeakResult &operator=(const SpeakResult &) = delete;

    Core::JSON::DecSInt64 SpeechId;
  };

  struct StateInfo : public Core::JSON::Container {
    StateInfo() : Core::JSON::Container(), State(false) {
      Add(_T("state"), &State);
    }
    StateInfo(const StateInfo &other)
        : Core::JSON::Container(), State(other.State) {
      Add(_T("state"), &State);
    }
    StateInfo &operator=(const StateInfo &) = delete;

    Core::JSON::Boolean State;
  };

  void OnCancelResult(const Core::JSON::String &,
                      const Core::JSONRPC::Error *) {}

  void OnStateChanged(const StateInfo &info) {
    is_enabled_.store(info.State.Value());
  }

  void OnSpeakResult(const SpeakResult &result,
                     const Core::JSONRPC::Error *err) {
    ::starboard::ScopedLock lock(mutex_);
    if (err) {
      SB_LOG(ERROR) << "TTS speak request failed. Error code: "
                    << err->Code.Value() << " message: " << err->Text.Value();
      speech_id_ = -1;
    } else {
      speech_id_ = result.SpeechId;
    }
    --speech_request_num_;
    condition_.Broadcast();
  }

public:
  TextToSpeechImpl() {
    uint32_t rc;
    rc =
        tts_link_.Subscribe<StateInfo>(kDefaultTimeoutMs, "onttsstatechanged",
                                       &TextToSpeechImpl::OnStateChanged, this);
    if (Core::ERROR_NONE != rc) {
      SB_LOG(ERROR) << "Failed to subscribe to '" << kTTSCallsign
                    << ".onttsstatechanged' event, rc=" << rc << " ( "
                    << Core::ErrorToString(rc) << " )";
    }

    IsTTSEnabledInfo info;
    rc = tts_link_.Get(kDefaultTimeoutMs, "isttsenabled", info);
    if (Core::ERROR_NONE == rc) {
      is_enabled_.store(info.IsEnabled.Value());
    }
  }

  void Speak(const std::string &text) {
    if (!is_enabled_.load())
      return;

    JsonObject params;
    params.Set(_T("text"), text);

    uint64_t rc = tts_link_.Dispatch(kDefaultTimeoutMs, "speak", params,
                                     &TextToSpeechImpl::OnSpeakResult, this);
    if (Core::ERROR_NONE == rc) {
      ::starboard::ScopedLock lock(mutex_);
      ++speech_request_num_;
    }
  }

  void Cancel() {
    if (!is_enabled_.load())
      return;

    int64_t speechId = -1;

    {
      ::starboard::ScopedLock lock(mutex_);
      if (speech_request_num_ != 0) {
        if (!condition_.WaitTimed(kSbTimeMillisecond) ||
            speech_request_num_ != 0)
          return;
      }
      speechId = speech_id_;
    }

    if (speechId < 0)
      return;

    JsonObject params;
    params.Set(_T("speechid"), speechId);

    tts_link_.Dispatch(kDefaultTimeoutMs, "cancel", params,
                       &TextToSpeechImpl::OnCancelResult, this);
  }

  bool IsEnabled() { return is_enabled_.load(); }
};

SB_ONCE_INITIALIZE_FUNCTION(TextToSpeechImpl, GetTextToSpeech);

} // namespace

struct DisplayInfo::Impl {
  Impl();
  ~Impl();
  ResolutionInfo GetResolution() {
    Refresh();
    char out_value[20];
    int value_length = 20;
    if (AmlDeviceGetProperty("COBALT_FORCE_SUPPORT_4K", out_value, value_length) ==
          AMLDEVICE_SUCCESS) {
      if ((SbStringCompareNoCaseN(out_value, "y", 1) == 0) ||
          (SbStringCompareNoCaseN(out_value, "Y", 1) == 0)) {
        // force to 4k
        resolution_info_.Width = 3840;
        resolution_info_.Height = 2160;
      } else if ((SbStringCompareNoCaseN(out_value, "n", 1) == 0) ||
                 (SbStringCompareNoCaseN(out_value, "N", 1) == 0)) {
        // force to FUD
        resolution_info_.Width = 1920;
        resolution_info_.Height = 1080;
      }
    }
    return resolution_info_;
  }
  bool HasHDRSupport() {
    Refresh();
    const char *force_support_HDR = getenv("COBALT_FORCE_SUPPORT_HDR");
    if (force_support_HDR) {
      if ((SbStringCompareNoCaseN(force_support_HDR, "y", 1) == 0) ||
          (SbStringCompareNoCaseN(force_support_HDR, "Y", 1) == 0)) {
        has_hdr_support_ = true;
      }
    }
    return has_hdr_support_;
  }
  float GetDiagonalSizeInInches() {
    Refresh();
    if (diagonal_size_in_inches_ == 0.f) {
      // For TV project
      // check the device properties TV_PANEL_SIZE
      char out_value[20];
      int value_length = 20;
      if (AmlDeviceGetProperty("TV_PANEL_SIZE", out_value, value_length) ==
          AMLDEVICE_SUCCESS) {
        diagonal_size_in_inches_ = atof(out_value);
      }
    }
    return diagonal_size_in_inches_;
  }

private:
  void Refresh();
  void OnUpdated(const Core::JSON::String &);

  ServiceLink display_info_;
  ResolutionInfo resolution_info_{};
  bool has_hdr_support_{false};
  float diagonal_size_in_inches_{0.f};
  ::starboard::atomic_bool needs_refresh_{true};
  ::starboard::atomic_bool did_subscribe_{false};
};

DisplayInfo::Impl::Impl() : display_info_(kDisplayInfoCallsign) { Refresh(); }

DisplayInfo::Impl::~Impl() {
  display_info_.Unsubscribe(kDefaultTimeoutMs, "updated");
}

void DisplayInfo::Impl::Refresh() {
  if (!needs_refresh_.load())
    return;

  uint32_t rc;

  if (!did_subscribe_.load()) {
    bool old_val = did_subscribe_.exchange(true);
    if (old_val == false) {
      rc = display_info_.Subscribe<Core::JSON::String>(
          kDefaultTimeoutMs, "updated", &DisplayInfo::Impl::OnUpdated, this);
      if (Core::ERROR_UNAVAILABLE == rc || kPriviligedRequestErrorCode == rc) {
        needs_refresh_.store(false);
        SB_LOG(ERROR) << "Failed to subscribe to '" << kDisplayInfoCallsign
                      << ".updated' event, rc=" << rc << " ( "
                      << Core::ErrorToString(rc) << " )";
        return;
      }
      if (Core::ERROR_NONE != rc) {
        did_subscribe_.store(false);
        SB_LOG(ERROR) << "Failed to subscribe to '" << kDisplayInfoCallsign
                      << ".updated' event, rc=" << rc << " ( "
                      << Core::ErrorToString(rc) << " )."
                      << " Going to try again next time.";
        display_info_.Unsubscribe(kDefaultTimeoutMs, "updated");
      }
    }
  }

  needs_refresh_.store(false);

  Core::JSON::EnumType<Exchange::IPlayerProperties::PlaybackResolution>
      resolution;
  rc = ServiceLink(kPlayerInfoCallsign)
           .Get(kDefaultTimeoutMs, "resolution", resolution);
  if (Core::ERROR_NONE == rc) {
    switch (resolution) {
    case Exchange::IPlayerProperties::RESOLUTION_2160P30:
    case Exchange::IPlayerProperties::RESOLUTION_2160P60:
      resolution_info_ = ResolutionInfo{3840, 2160};
      break;
    case Exchange::IPlayerProperties::RESOLUTION_1080I:
    case Exchange::IPlayerProperties::RESOLUTION_1080P:
    case Exchange::IPlayerProperties::RESOLUTION_UNKNOWN:
      resolution_info_ = ResolutionInfo{1920, 1080};
      break;
    default:
      resolution_info_ = ResolutionInfo{1280, 720};
      break;
    }
  } else {
    resolution_info_ = ResolutionInfo{1920, 1080};
    SB_LOG(ERROR) << "Failed to get 'resolution', rc=" << rc << " ( "
                  << Core::ErrorToString(rc) << " )";
  }

  Core::JSON::DecUInt16 widthincentimeters, heightincentimeters;
  rc = display_info_.Get(kDefaultTimeoutMs, "widthincentimeters",
                         widthincentimeters);
  if (Core::ERROR_NONE != rc) {
    widthincentimeters.Clear();
    SB_LOG(ERROR) << "Failed to get 'DisplayInfo.widthincentimeters', rc=" << rc
                  << " ( " << Core::ErrorToString(rc) << " )";
  }

  rc = display_info_.Get(kDefaultTimeoutMs, "heightincentimeters",
                         heightincentimeters);
  if (Core::ERROR_NONE != rc) {
    heightincentimeters.Clear();
    SB_LOG(ERROR) << "Failed to get 'DisplayInfo.heightincentimeters', rc="
                  << rc << " ( " << Core::ErrorToString(rc) << " )";
  }

  if (widthincentimeters && heightincentimeters) {
    diagonal_size_in_inches_ =
        sqrtf(powf(widthincentimeters, 2) + powf(heightincentimeters, 2)) /
        2.54f;
  } else {
    diagonal_size_in_inches_ = 0.f;
  }

  auto detectHdr10Support = [&]() {
    using Caps = Core::JSON::ArrayType<
        Core::JSON::EnumType<Exchange::IHDRProperties::HDRType>>;

    Caps tvcapabilities;
    rc = display_info_.Get(kDefaultTimeoutMs, "tvcapabilities", tvcapabilities);
    if (Core::ERROR_NONE != rc) {
      SB_LOG(ERROR) << "Failed to get 'tvcapabilities', rc=" << rc << " ( "
                    << Core::ErrorToString(rc) << " )";
      return false;
    }

    bool tvHasHDR10 = false;
    {
      Caps::Iterator index(tvcapabilities.Elements());
      while (index.Next() && !tvHasHDR10)
        tvHasHDR10 = (index.Current() == Exchange::IHDRProperties::HDR_10);
    }
    if (false == tvHasHDR10) {
      SB_LOG(INFO) << "No HDR10 in TV caps";
      return false;
    }

    Caps stbcapabilities;
    rc = display_info_.Get(kDefaultTimeoutMs, "stbcapabilities",
                           stbcapabilities);
    if (Core::ERROR_NONE != rc) {
      SB_LOG(ERROR) << "Failed to get 'stbcapabilities', rc=" << rc << " ( "
                    << Core::ErrorToString(rc) << " )";
      return false;
    }

    bool stbHasHDR10 = false;
    {
      Caps::Iterator index(stbcapabilities.Elements());
      while (index.Next() == true && stbHasHDR10 == false)
        stbHasHDR10 = (index.Current() == Exchange::IHDRProperties::HDR_10);
    }
    if (false == stbHasHDR10) {
      SB_LOG(INFO) << "No HDR10 in STB caps";
      return false;
    }

    return stbHasHDR10;
  };

  has_hdr_support_ = detectHdr10Support();

  SB_LOG(INFO) << "Display info updated, resolution: " << resolution_info_.Width
               << 'x' << resolution_info_.Height
               << ", has hdr: " << (has_hdr_support_ ? "yes" : "no")
               << ", diagonal size in inches: " << diagonal_size_in_inches_;
}

void DisplayInfo::Impl::OnUpdated(const Core::JSON::String &) {
  if (needs_refresh_.load() == false) {
    needs_refresh_.store(true);
    SbEventSchedule(
        [](void *data) { Application::Get()->DisplayInfoChanged(); }, nullptr,
        0);
  }
}

DisplayInfo::DisplayInfo() : impl_(new Impl) {}

DisplayInfo::~DisplayInfo() {}

ResolutionInfo DisplayInfo::GetResolution() const {
  return impl_->GetResolution();
}

float DisplayInfo::GetDiagonalSizeInInches() const {
  return impl_->GetDiagonalSizeInInches();
}

bool DisplayInfo::HasHDRSupport() const { return impl_->HasHDRSupport(); }

std::string DeviceIdentification::GetChipset() {
  return GetDeviceIdImpl()->chipset;
}

std::string DeviceIdentification::GetFirmwareVersion() {
  return GetDeviceIdImpl()->firmware_version;
}

struct HdcpProfile::HdcpProfileImpl {
private:
  bool has_hdmi_connect_{true};
  bool hdmi_hotplug_{true};
  ServiceLink hdpc_link_{kHdcpProfileCallsign};
  RDKShellInfo rdkShellInfo;
  void StatusUpdated(const Core::JSON::String &data) {

    if (!hdmi_hotplug_)
      return;

    // focusAppName init failed in rdkshellInfo
    if (rdkShellInfo.ImpGetFocusAppName().empty()) {
      SB_LOG(ERROR) << "focusAppName is empty!";
      return;
    }

    bool hdcpstatus = true;
    JsonObject hdcpStatus(data.Value());
    JsonObject hdcpConnect(hdcpStatus["HDCPStatus"].Value());

    if (!hdcpConnect["isConnected"].Value().compare("true") &&
        !hdcpConnect["isHDCPCompliant"].Value().compare("true"))
      hdcpstatus = true;
    else
      hdcpstatus = false;

    if (hdcpstatus != has_hdmi_connect_) {
      has_hdmi_connect_ = hdcpstatus;
      if (hdcpstatus) {
        // When cobalt is not in the focus state or focus app is launcher,
        // there is no need to respond to the callback,
        // otherwise it will cause confusion.
        if ((!rdkShellInfo.ImpGetFocusStatus()) && (rdkShellInfo.ImpGetFocusAppName() !=  "launcher")) {
          SB_LOG(INFO) << "Skip hdcpstatus handle, focusAppName is " << rdkShellInfo.ImpGetFocusAppName();
          return;
        }

        bool focusstatus = Application::Get()->GetFocusStatus();

        SbEventSchedule(
            [](void *data) {
              Application::Get()->SendUnfreezeEvent();
              Application::Get()->SendRevealEvent();
            },
            nullptr, 0);
        if (focusstatus) {
          SbEventSchedule(
              [](void *data) { Application::Get()->SendFocusEvent(); }, nullptr,
              0);
        }
      } else {
        SbEventSchedule(
            [](void *data) {
              Application::Get()->SendBlurEvent();
              Application::Get()->SendConcealEvent();
              Application::Get()->SendFreezeEvent();
            },
            nullptr, 0);
      }
    }
  }

public:
  HdcpProfileImpl() {
    const char *support_hdmihotplug = getenv("HDMIHOTPLUG_SUPPORT");
    if (support_hdmihotplug) {
      if ((SbStringCompareNoCaseN(support_hdmihotplug, "n", 1) == 0) ||
          (SbStringCompareNoCaseN(support_hdmihotplug, "N", 1) == 0)) {
        hdmi_hotplug_ = false;
      }
    }
    uint32_t rc = hdpc_link_.Subscribe<Core::JSON::String>(
        kDefaultTimeoutMs, "onDisplayConnectionChanged",
        &HdcpProfileImpl::StatusUpdated, this);
    if (Core::ERROR_NONE != rc) {
      SB_LOG(ERROR) << "Failed to subscribe to '" << kHdcpProfileCallsign
                    << ".onDisplayConnectionChanged' event, rc=" << rc << " ( "
                    << Core::ErrorToString(rc) << " )";
    }
    Core::JSON::String data;
    rc = hdpc_link_.Get(kDefaultTimeoutMs, "getHDCPStatus", data);
    if (Core::ERROR_NONE == rc) {
      StatusUpdated(data);
    }
  }
  ~HdcpProfileImpl() {
    hdpc_link_.Unsubscribe(kDefaultTimeoutMs, "onDisplayConnectionChanged");
  }
};

HdcpProfile::HdcpProfile() : impl_(new HdcpProfileImpl) {}

HdcpProfile::~HdcpProfile() {}
struct NetworkInfo::NetworkInfoImpl {
private:
  ServiceLink networkinfo_link_{kNetworkCallsign};
  bool wifi_connected_{false};
  bool eth_connected_{false};
  bool connect_status_{false};

  void StatusUpdated(const JsonObject &data) {
    if (0 == data.Get("interface").Value().compare("WIFI")) {
      if (0 == data.Get("status").Value().compare("CONNECTED")) {
        wifi_connected_ = true;
      } else if (0 == data.Get("status").Value().compare("DISCONNECTED")) {
        wifi_connected_ = false;
      }
    } else if (0 == data.Get("interface").Value().compare("ETHERNET")) {
      if (0 == data.Get("status").Value().compare("CONNECTED")) {
        eth_connected_ = true;
      } else if (0 == data.Get("status").Value().compare("DISCONNECTED")) {
        eth_connected_ = false;
      }
    }
    if (wifi_connected_ || eth_connected_) {
      if (!connect_status_)
        Application::Get()->SendNetworkConnectEvent();
      connect_status_ = true;
    }
    if ((!wifi_connected_) && (!eth_connected_)) {
      if (connect_status_)
        Application::Get()->SendNetworkDisconnectEvent();
      connect_status_ = false;
    }
  }

public:
  NetworkInfoImpl() {

    uint32_t rc = networkinfo_link_.Subscribe<JsonObject>(
        kDefaultTimeoutMs, "onConnectionStatusChanged",
        &NetworkInfoImpl::StatusUpdated, this);
    if (Core::ERROR_NONE != rc) {
      SB_LOG(ERROR) << "Failed to subscribe to '" << kNetworkCallsign
                    << ".onConnectionStatusChanged' event, rc=" << rc << " ( "
                    << Core::ErrorToString(rc) << " )";
    }
    JsonObject data;
    rc = ServiceLink(kNetworkCallsign)
             .Get(kDefaultTimeoutMs, "getDefaultInterface", data);
    if (Core::ERROR_NONE == rc) {
      if (0 == data.Get("interface").Value().compare("WIFI")) {
        wifi_connected_ = true;
        connect_status_ = true;
      }
      if (0 == data.Get("interface").Value().compare("ETHERNET")) {
        eth_connected_ = true;
        connect_status_ = true;
      }
    }
  }

  ~NetworkInfoImpl() {
    networkinfo_link_.Unsubscribe(kDefaultTimeoutMs,
                                  "onConnectionStatusChanged");
  }
};

NetworkInfo::NetworkInfo() : impl_(new NetworkInfoImpl) {}

NetworkInfo::~NetworkInfo() {}
bool NetworkInfo::IsConnectionTypeWireless() {
  JsonObject data;
  uint32_t rc = ServiceLink(kNetworkCallsign)
                    .Get(kDefaultTimeoutMs, "getDefaultInterface", data);
  if (Core::ERROR_NONE == rc)
    return (0 == data.Get("interface").Value().compare("WIFI"));
  return false;
}

void TextToSpeech::Speak(const std::string &text) {
  GetTextToSpeech()->Speak(text);
}

bool TextToSpeech::IsEnabled() { return GetTextToSpeech()->IsEnabled(); }

void TextToSpeech::Cancel() { GetTextToSpeech()->Cancel(); }

struct RDKShellInfo::RDKShellInfoImpl {
private:
  ServiceLink rdkshellinfo_link_ { kRDKShellCallsign };
  bool   focus_status_ {false};
  string appName_ {""};
  string focusAppName_ {""};
  //::starboard::Mutex mutex_;

void  onFocusStatus(const JsonObject& data) {
  // ::starboard::ScopedLock lock(mutex_);
  std::string client = data.Get("client").Value();
  if (!client.empty()) {
    focusAppName_ = client;
    if ( appName_ == client) {
      // get focus
      if (!focus_status_) {
        focus_status_ = true;
        SbEventSchedule([](void* data) {
          Application::Get()->SendFocusEvent();
        }, nullptr, 0);
      }
    } else {
      // lost focus
      if (focus_status_) {
        focus_status_ = false;
        SbEventSchedule([](void* data) {
          Application::Get()->SendBlurEvent();
        }, nullptr, 0);
      }
    }
  }
}

public:
bool getfocusstatus(void)
{
  return focus_status_;
}

std::string getfocusAppName(void)
{
  return focusAppName_;
}

RDKShellInfoImpl() {
  uint32_t rc;
  // Get current app name
  const char*  self_name = std::getenv("CLIENT_IDENTIFIER");
  if (self_name) {
    appName_ = string(self_name);

    int pos = appName_.find(",");
    if (string::npos != pos)
      appName_ = appName_.substr(0, pos);
  } else
    appName_ = string("unknow");
  std::transform(appName_.begin(),appName_.end(),appName_.begin(),tolower);

  // Get focus for default focus status
  JsonObject data;
  rc = ServiceLink(kRDKShellCallsign).Get(kDefaultTimeoutMs, "getFocused", data);
  if (Core::ERROR_NONE == rc)
  {
    focusAppName_ = data.Get("client").Value();
    if (appName_  == focusAppName_) {
      focus_status_ = true;
      SbEventSchedule([](void* data) {
        Application::Get()->SendFocusEvent();
      }, nullptr, 0);
    }
  }

  // Subscribe focus change callback.
  rc = rdkshellinfo_link_.Subscribe<JsonObject>(kDefaultTimeoutMs, "onApplicationFocusChanged", &RDKShellInfoImpl::onFocusStatus, this);
  if (Core::ERROR_NONE != rc) {
    SB_LOG(ERROR)
      << "Failed to subscribe to '" << kRDKShellCallsign
      << ".onConnectionStatusChanged' event, rc=" << rc
      << " ( " << Core::ErrorToString(rc) << " )";
  }
}
  ~RDKShellInfoImpl() {
    rdkshellinfo_link_.Unsubscribe(kDefaultTimeoutMs,
                                   "onApplicationFocusChanged");
  }
};

RDKShellInfo::RDKShellInfo() : impl_(new RDKShellInfoImpl) {}

RDKShellInfo::~RDKShellInfo() {}
bool RDKShellInfo::ImpGetFocusStatus() { return impl_->getfocusstatus(); }
std::string RDKShellInfo::ImpGetFocusAppName() { return impl_->getfocusAppName(); }

struct VoiceInput::VoiceInputImpl {
private:
  ServiceLink voiceinput_link_{kVoiceInputCallsign};
  ::starboard::Mutex m_mutex;
  enum State { state_stop, state_start, state_data_buffering };
  State m_step{state_stop}; // 0: stop,  1: start,  2: data
  bool m_micFlag{true};     // 0: ble, 1, mic
  size_t m_writePos{0};
  size_t m_readPos{0};
  uint8_t *m_audioBuf{nullptr};
  size_t m_audioBufLen{100 * 1024};
  size_t m_totalReadLen{0};
  size_t m_totalWriteLen{0};
  bool m_blStop{false};

  void resetParameters() {
    m_step = state_stop;
    m_totalReadLen = 0;
    m_totalWriteLen = 0;
    m_writePos = 0;
    m_readPos = 0;
    m_micFlag = true;
    m_blStop = false;
  }

  void StatusUpdated(const JsonObject &data) {
    if (0 == data.Get("source").Value().compare("mic")) {
      m_micFlag = true;
    } else if (data.Get("source").Value().compare("ble") == 0) {
      m_micFlag = false;
    }

    if (0 == data.Get("action").Value().compare("start")) {
      /* for ble, stop event send when release RC button.
      cobalt may not read all data when get stop event here.
      add m_blstop to reset parameters after stop event and
       remaining buffer is 0.
      add if(!m_micFlag && m_blStop) here to avoid cobalt stop reading
       while data buffering still have data.
      */
      if (m_step == state_stop && m_blStop) {
        bool micFlag = m_micFlag;
        resetParameters();
        m_micFlag = micFlag;
      }
      m_step = state_start;

      if (!m_micFlag) {
        Application::Get()->SendMicTriggerEvent();
      }
    } else if (0 == data.Get("action").Value().compare("stop")) {
      if (m_micFlag)
        resetParameters();
      else {
        m_blStop = true;
        m_step = state_stop;
      }

    } else if (0 == data.Get("action").Value().compare("data")) {
      if (m_step == state_start) {
        m_step = state_data_buffering;
      }
    }

    SB_LOG(WARNING) << "cobalt: status update: mic flag" << m_micFlag
                  << " step:" << m_step<< " data size : " << data.Get("data").Value().size();

    if (m_step == state_data_buffering) {
      string micData = data.Get("data").Value();

      if (micData.size() == 0)
        return;

      size_t dataSize = micData.size();
      uint8_t *realData = new uint8_t[dataSize];
      if (realData == nullptr) {
        SB_LOG(ERROR) << "mic process:  malloc mem failed.";
        return;
      }

      uint16_t convertSize;
      size_t writePos = m_writePos;
      convertSize = dataSize;
      Core::FromString(micData, realData, convertSize, nullptr);
      dataSize = convertSize;
      int remaining = m_audioBufLen - writePos;
      if (remaining > dataSize) {
        memcpy(&m_audioBuf[writePos], realData, dataSize);
        writePos += dataSize;
      } else {
        memcpy(&m_audioBuf[writePos], realData, remaining);
        writePos = dataSize - remaining;
        if (writePos)
          memcpy(&m_audioBuf[0], realData + remaining, writePos);
      }
      {
        ::starboard::ScopedLock lock(m_mutex);
        m_totalWriteLen += dataSize;
        m_writePos = writePos;
      }
      delete[] realData;
    }
  }

public:
  VoiceInputImpl() {
    if (!getMicroPhoneEnable())
      return;
    m_audioBuf = new uint8_t[m_audioBufLen];
    uint32_t rc = voiceinput_link_.Subscribe<JsonObject>(
        kDefaultTimeoutMs, "onVoiceInputStatusChanged",
        &VoiceInputImpl::StatusUpdated, this);
    if (Core::ERROR_NONE != rc) {
      SB_LOG(ERROR) << "Failed to subscribe to '" << kVoiceInputCallsign
                    << ".onVoiceInputStatusChanged' event, rc=" << rc << " ( "
                    << Core::ErrorToString(rc) << " )";
    }
  }

  ~VoiceInputImpl() {
    if (!getMicroPhoneEnable())
      return;

    voiceinput_link_.Unsubscribe(kDefaultTimeoutMs,
                                 "onVoiceInputStatusChanged");
    delete m_audioBuf;
  }

  int getData(void *data, int size) {
    int totalWriteLen;
    int writePos;
    bool blStop = false;
    {
      ::starboard::ScopedLock lock(m_mutex);
      totalWriteLen = m_totalWriteLen;
      writePos = m_writePos;
      if (!m_micFlag && m_blStop)
        blStop = true;
    }
    SB_LOG(WARNING) << "write len: " << totalWriteLen
                  << " read len: " << m_totalReadLen << " data size: " << size;
    if (totalWriteLen <= m_totalReadLen) {
      if (blStop) {
        resetParameters();
      }
      return 0;
    }

    int copyLen;
    int posDiff;

    copyLen = totalWriteLen - m_totalReadLen;
    copyLen = copyLen > size ? size : copyLen;
    posDiff = writePos > m_readPos ? (writePos - m_readPos)
                                   : (writePos + m_audioBufLen - m_readPos);

    if (copyLen > posDiff) {
      SB_LOG(ERROR) << kVoiceInputCallsign
                    << "Not enough buffer size for voice input data";
    }

    if (m_readPos + copyLen < m_audioBufLen) {
      memcpy(data, &m_audioBuf[m_readPos], copyLen);
      m_readPos += copyLen;
    } else {
      int remaining = m_audioBufLen - m_readPos;
      memcpy(data, &m_audioBuf[m_readPos], remaining);
      m_readPos = copyLen - remaining;
      if (m_readPos) {
        memcpy((uint8_t *)data + remaining, &m_audioBuf[0], m_readPos);
      }
    }
    m_totalReadLen += copyLen;
    SB_LOG(WARNING) << "get data:" << copyLen << " read pos:" << m_readPos
                  << "total len:" << m_totalReadLen;
    return copyLen;
  }

  static bool getMicroPhoneEnable() {
    char out_value[32];
    int value_length = 32;
    bool defaultEnable = true;  // default as enable if no setting in device properties

    if (AmlDeviceGetProperty("COBALT_MICROPHONE",
          out_value, value_length) != AMLDEVICE_SUCCESS) {
      return defaultEnable;
    }

    out_value[0] = out_value[0] == 'n' ? 'N' : out_value[0];
    if (!strncmp(out_value, "N", strlen("N")))
      return false;
    return defaultEnable;
  }
};

VoiceInput *VoiceInput::_instance = nullptr;
VoiceInput::VoiceInput() : impl_(new VoiceInputImpl){
  if (!VoiceInputImpl::getMicroPhoneEnable())
    return;
  VoiceInput::_instance = this;
}

VoiceInput::~VoiceInput() { VoiceInput::_instance = nullptr; }

int VoiceInput::GetData(void *data, int size) {

  if (VoiceInput::_instance == nullptr)
    return 0;

  return VoiceInput::_instance->impl_->getData(data, size);
}

bool VoiceInput::StartRecord() {
  JsonObject data;
  uint32_t rc = ServiceLink(kVoiceInputCallsign)
                    .Get(kDefaultTimeoutMs, "startCapture", data);
  if (Core::ERROR_NONE != rc) {
    SB_LOG(ERROR) << "Failed to startCapture for callsign : '"
                  << kVoiceInputCallsign << "'. rc=" << rc << " ( "
                  << Core::ErrorToString(rc) << " )";
    return false;
  }
  return true;
}

bool VoiceInput::StopRecord() {
  JsonObject data;
  uint32_t rc = ServiceLink(kVoiceInputCallsign)
                    .Get(kDefaultTimeoutMs, "stopCapture", data);
  if (Core::ERROR_NONE != rc) {
    SB_LOG(ERROR) << "Failed to stopCapture for callsign : '"
                  << kVoiceInputCallsign << "'. rc=" << rc << " ( "
                  << Core::ErrorToString(rc) << " )";
    return false;
  }
  return true;
}

bool VoiceInput::isSampleRateSupport(int sample_rate) {
  uint32_t rc;
  JsonObject params;
  JsonObject result;
  bool support = false;

  params.Set("samplerate", sample_rate);
  std::shared_ptr<
      WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>>
      samplerateSupport;
  samplerateSupport = std::make_shared<
      WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>>(
      "org.rdk.VoiceInput.1", "", false, "");
  if (samplerateSupport == nullptr)  {
    SB_LOG(ERROR) << "voiceinput: get plugin info failed";
    return false;
  }

  rc = samplerateSupport->Invoke(2000, "isSamplerateSupport", params, result);

  if (rc != Core::ERROR_NONE) {
    SB_LOG(ERROR) << "isSampleRateSupport failed" << rc << " ( "
                  << Core::ErrorToString(rc) << " )";
  } else {
    support = result.Get("support").Value().compare("true") == 0;
    SB_LOG(WARNING) << "isSampleRateSupport samplerate:" << sample_rate
                  << "result: " << result.Get("support").Value();
  }

  return support;
}

bool VoiceInput::isMuted() {
  JsonObject data;
  uint32_t rc;

  rc = ServiceLink(kVoiceInputCallsign).Get(kDefaultTimeoutMs, "getMute", data);

  if (Core::ERROR_NONE == rc) {
    SB_LOG(WARNING) << "check micro phone is muted:  "
                  << data.Get("mute").Value();
    if (data.Get("mute").Value().compare("true") == 0) {
      return true;
    } else {
      return false;
    }
  } else {
    SB_LOG(ERROR) << "Failed to get isMuted for callsign : '"
                  << kVoiceInputCallsign << "'. rc=" << rc << " ( "
                  << Core::ErrorToString(rc) << " )";
  }
  return false;
}

int VoiceInput::GetSampleRate() {
  uint32_t rc;
  JsonObject data;
  rc = ServiceLink(kVoiceInputCallsign)
           .Get(kDefaultTimeoutMs, "getSampleRate", data);
  if (Core::ERROR_NONE == rc) {
    int sampleRate = 16000;
    if (data.HasLabel("samplerate")) {
      sampleRate = data["samplerate"].Number();
    }
    SB_LOG(WARNING) << "getSamplerate" << data.Get("samplerate").Value() << ":"
                  << sampleRate;
    return sampleRate;
  } else {
    SB_LOG(ERROR) << "Failed to get GetSampleRate for callsign : '"
                  << kVoiceInputCallsign << "'. rc=" << rc << " ( "
                  << Core::ErrorToString(rc) << " )";
  }
  return 16000; // default as 16k samplerate?
}

int VoiceInput::GetMicroPhoneEnable() {
  return VoiceInputImpl::getMicroPhoneEnable();
}

} // namespace shared
} // namespace rdk
} // namespace starboard
} // namespace third_party
