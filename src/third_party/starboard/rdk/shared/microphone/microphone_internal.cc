// Copyright 2018 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/shared/starboard/microphone/microphone_internal.h"
#include "third_party/starboard/rdk/shared/log_override.h"
#include "third_party/starboard/rdk/shared/rdkservices.h"

#include <algorithm>
#include <cstddef>
#include <queue>
#include <string.h>

#include "starboard/common/log.h"
#include "starboard/common/mutex.h"
#include "starboard/common/scoped_ptr.h"
#include "starboard/memory.h"
#include "starboard/shared/starboard/thread_checker.h"
#include "starboard/time.h"

#include "aml_device_property.h"

namespace starboard {
namespace microphone {
namespace shared {
namespace {
const int kSampleRateInHz = 16000;
const int kSamplesPerBuffer = 128;
} // namespace

class SbMicrophoneImpl : public SbMicrophonePrivate {
public:
  SbMicrophoneImpl();
  ~SbMicrophoneImpl() override;

  bool Open() override;
  bool Close() override;
  int Read(void *out_audio_data, int audio_data_size) override;

  static bool IsMicrophoneDisconnected();
  static bool IsMicrophoneMute();
  static int getSampleRate();
  static bool getMicrophoneEnable();

private:
  enum State { kOpened, kClosed };

  bool StartRecording();
  bool StopRecording();

  // Keeps track of the microphone's current state.
  State state_;

  SbTime audio_duration_;
  SbTime audio_interval_;
  SbTime pre_audio_read_;
};

SbMicrophoneImpl::SbMicrophoneImpl() : state_(kClosed) {}

SbMicrophoneImpl::~SbMicrophoneImpl() { Close(); }

// static
bool SbMicrophoneImpl::IsMicrophoneDisconnected() {
  // Not implement
  return false;
}

// static
bool SbMicrophoneImpl::IsMicrophoneMute() {
  return third_party::starboard::rdk::shared::VoiceInput::isMuted();
}

int SbMicrophoneImpl::getSampleRate() {
  return third_party::starboard::rdk::shared::VoiceInput::GetSampleRate();
}

bool SbMicrophoneImpl::getMicrophoneEnable() {
  return third_party::starboard::rdk::shared::VoiceInput::GetMicroPhoneEnable();
}

bool SbMicrophoneImpl::Open() {
  SB_LOG(WARNING) << "micInternal: SbMicrophoneImpl::Open state_ = " << state_;
  // for ble, after mic trigger.  cobalt will call open function also
  if (state_ == kOpened) {
    return true;
  }

  if (IsMicrophoneDisconnected()) {
    SB_LOG(ERROR) << "micInternal: No microphone connected.";
    return false;
  } else if (!StartRecording()) {
    SB_LOG(ERROR) << "micInternal: Error starting recording.";
    return false;
  }

  // Successfully opened the microphone and started recording.
  state_ = kOpened;
  SB_LOG(WARNING) << "micInternal: SbMicrophoneImpl::Open successfully state_ = "
                << state_;
  return true;
}

bool SbMicrophoneImpl::StartRecording() {
  bool flag = third_party::starboard::rdk::shared::VoiceInput::StartRecord();
  if (flag) {
    pre_audio_read_ = SbTimeGetMonotonicNow();
    audio_interval_ = 0; // Microseconds, depends on sampleRate, buffersize of read data.
  }
  SB_LOG(WARNING) << "micInternal: start recording:" << flag;
  return flag;
}

bool SbMicrophoneImpl::Close() {
  if (state_ == kClosed) {
    return true;
  }

  if (state_ == kOpened && !StopRecording()) {
    SB_LOG(ERROR) << "micInternal: Error closing the microphone.";
    return false;
  }

  state_ = kClosed;
  SB_LOG(WARNING) << "micInternal: SbMicrophoneImpl::Close ";
  return true;
}

bool SbMicrophoneImpl::StopRecording() {
  return third_party::starboard::rdk::shared::VoiceInput::StopRecord();
}

int SbMicrophoneImpl::Read(void *out_audio_data, int audio_data_size) {
  if (state_ == kClosed || IsMicrophoneMute()) {
    // return error as no data from a stopped/muted microphone.
    return -1;
  }
  if (audio_data_size < 0) {
    SB_LOG(ERROR) << "micInternal:: invalid read data size";
    return -1;
  }
// for 16000, 16_b, mono, 8k means 250ms data.
// bigger size of buffer means more time for buffering data.
  if (audio_data_size > 8192)
    audio_data_size = 8192;

  if (audio_interval_ == 0) {
    // use samplerate 16000 and channel mono, pcm_16_b,
    // need to change if mic use other parameters.
    audio_interval_ = kSbTimeSecond * audio_data_size / (16000 * 2);
  } else {
    SbTime cur_time = SbTimeGetMonotonicNow();
    if (cur_time < (pre_audio_read_ + audio_interval_)) {
      return 0;
    } else {
      pre_audio_read_ += audio_interval_;
    }
  }

  return third_party::starboard::rdk::shared::VoiceInput::GetData(
      out_audio_data, audio_data_size);
}

} // namespace shared
} // namespace microphone
} // namespace starboard

int SbMicrophonePrivate::GetAvailableMicrophones(
    SbMicrophoneInfo *out_info_array, int info_array_size) {
  if (!starboard::microphone::shared::SbMicrophoneImpl::getMicrophoneEnable())
    return 0;

  // Note that there is no way of checking for a connected microphone/device
  // before API 23, so GetAvailableMicrophones() will assume a microphone is
  // connected and always return 1 on APIs < 23.
  SB_LOG(WARNING) << "micInternal:  SbMicrophoneImpl::GetAvailableMicrophones enter";

  if (starboard::microphone::shared::SbMicrophoneImpl::
          IsMicrophoneDisconnected()) {
    SB_LOG(ERROR) << "micInternal: No microphone connected.";
    return 0;
  }

  if (starboard::microphone::shared::SbMicrophoneImpl::IsMicrophoneMute()) {
    SB_LOG(ERROR) << "micInternal: microphone is muted.";
    return 0;
  }

  if (out_info_array && info_array_size > 0) {
    // Only support one microphone.
    out_info_array[0].id = reinterpret_cast<SbMicrophoneId>(1);
    out_info_array[0].type = kSbMicrophoneUnknown;
    out_info_array[0].max_sample_rate_hz = starboard::microphone::shared::
        SbMicrophoneImpl::getSampleRate(); // get sample rate from plugin.
    out_info_array[0].min_read_size =
        starboard::microphone::shared::kSamplesPerBuffer;
    SB_LOG(WARNING) << "samplerate :" << out_info_array[0].max_sample_rate_hz
                  <<"microphone type : " << out_info_array[0].type
                  << " micInternal: SbMicrophoneImpl::GetAvailableMicrophones "
                     "successfully ";
  }

  return 1;
}

bool SbMicrophonePrivate::IsMicrophoneSampleRateSupported(
    SbMicrophoneId id, int sample_rate_in_hz) {
  if (!SbMicrophoneIdIsValid(id)) {
    return false;
  }
  bool flag =
      third_party::starboard::rdk::shared::VoiceInput::isSampleRateSupport(
          sample_rate_in_hz);
  SB_LOG(WARNING) << "micInternal: isSampleRateSupport:" << flag
                << "sample rate:" << sample_rate_in_hz;
  return flag;
}

namespace {
const int kUnusedBufferSize = 32 * 1024;
// Only a single microphone is supported.
SbMicrophone s_microphone = kSbMicrophoneInvalid;

} // namespace

SbMicrophone SbMicrophonePrivate::CreateMicrophone(SbMicrophoneId id,
                                                   int sample_rate_in_hz,
                                                   int buffer_size_bytes) {
  if (!starboard::microphone::shared::SbMicrophoneImpl::getMicrophoneEnable())
    return kSbMicrophoneInvalid;
  SB_LOG(WARNING) << "micInternal:  create microphone enter";
  if (!SbMicrophoneIdIsValid(id) ||
      !IsMicrophoneSampleRateSupported(id, sample_rate_in_hz) ||
      buffer_size_bytes > kUnusedBufferSize || buffer_size_bytes <= 0) {
    return kSbMicrophoneInvalid;
  }

  if (s_microphone != kSbMicrophoneInvalid) {
    return kSbMicrophoneInvalid;
  }

  s_microphone = new starboard::microphone::shared::SbMicrophoneImpl();
  SB_LOG(ERROR) << "micInternal:  create microphone successfully";
  return s_microphone;
}

void SbMicrophonePrivate::DestroyMicrophone(SbMicrophone microphone) {
  SB_LOG(WARNING) << "micInternal:  destroy  microphone enter";
  if (!SbMicrophoneIsValid(microphone)) {
    return;
  }

  SB_DCHECK(s_microphone == microphone);
  s_microphone->Close();

  delete s_microphone;
  s_microphone = kSbMicrophoneInvalid;
  SB_LOG(ERROR) << "micInternal:  destroy  microphone successfully";
}
