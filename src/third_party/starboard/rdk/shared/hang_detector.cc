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
// SPDX-License-Identifier: Apache-2.0//
#include "third_party/starboard/rdk/shared/hang_detector.h"

#include <algorithm>
#include <vector>

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>

#include "starboard/once.h"
#include "starboard/thread.h"
#include "starboard/common/condition_variable.h"
#include "starboard/common/mutex.h"

#include "third_party/starboard/rdk/shared/log_override.h"

namespace third_party {
namespace starboard {
namespace rdk {
namespace shared {

namespace {

const uint32_t kMaxExpirationCount = 6;

void print_action(pid_t pid, const std::string& name) {
  fprintf(stderr, "\n*** Cobalt hang monitor expired!!! pid=%ld, monitor='%s'. Continue.\n", (long)pid, name.c_str());
}

void kill_action(pid_t pid, const std::string& name) {
  fprintf(stderr, "\n*** Hang detected in Cobalt!!! pid=%ld, monitor='%s'. sending SIGFPE \n", (long)pid, name.c_str());
  kill(pid, SIGFPE);

  SbThreadSleep( kSbTimeMinute );
  fprintf(stderr, "\n*** Hang detected in Cobalt!!! pid=%ld, monitor='%s'. still hanging... sending SIGKILL \n", (long)pid, name.c_str());

  kill(pid, SIGKILL);

  SbThreadSleep( kSbTimeMinute );
  fprintf(stderr, "\n*** Hang detected in Cobalt!!! pid=%ld, monitor='%s'. still hanging... give up \n", (long)pid, name.c_str());
}

SbTime get_check_interval() {
  const char* env = std::getenv("COBALT_HANG_DETECTOR_INTERVAL_IN_SECONDS");
  if( env ) {
    int64_t t = strtol(env, nullptr, 0);
    if ( 0 >= t )
      return kSbTimeMax;
    else
      return kSbTimeSecond * t;
  }
  return kSbTimeSecond * 30;
}

struct HangDetector
{
  static void* ThreadEntryPoint(void* context) {
    SB_DCHECK(context);
    static_cast<HangDetector*>(context)->DoWork();
    return nullptr;
  }

  HangDetector() {
    if ( check_interval_ == kSbTimeMax )
      return;

    thread_ =
      SbThreadCreate(0, kSbThreadNoPriority, kSbThreadNoAffinity, true,
                     "hangdetector_thread", &HangDetector::ThreadEntryPoint, this);
    SB_DCHECK(SbThreadIsValid(thread_));
  }

  ~HangDetector() {
    if (SbThreadIsValid(thread_)) {
      mutex_.Acquire();
      running_ = false;
      monitors_.clear();
      condition_.Broadcast();
      mutex_.Release();
      SbThreadJoin(thread_, nullptr);
    }
  }

  void DoWork() {
    SbTimeMonotonic next_check = 0;
    pid_t pid = getpid();

    for (;;) {
      // don't hog the cpu in case timed condition fails
      SbThreadSleep( kSbTimeMillisecond );

      ::starboard::ScopedLock lock(mutex_);
      if ( running_ == false )
        return;

      if ( condition_.WaitTimed( check_interval_ ) )
        continue;

      SbTimeMonotonic now = SbTimeGetMonotonicNow();
      if ( now > next_check ) {
        next_check = now + check_interval_;

        for (const auto& m : monitors_) {
          if ( m->GetExpirationTime() > now )
            continue;

          std::string name = m->Name();

          if ( m->IncExpirationCount() < kMaxExpirationCount ) {
            print_action( pid, name );
            continue;
          }

          mutex_.Release();
          kill_action( pid, name );
          mutex_.Acquire();

          running_ = false;
          SB_CHECK(false);
          break;
        }
      }
    }
  }

  void AddMonitor(HangMonitor *m) {
    ::starboard::ScopedLock lock(mutex_);
    monitors_.push_back(m);
  }

  void RemoveMonitor(HangMonitor *m) {
    ::starboard::ScopedLock lock(mutex_);
    monitors_.erase(std::remove(monitors_.begin(), monitors_.end(), m), monitors_.end());
  }

  SbTime GetCheckInterval() const {
    return check_interval_;
  }

  ::starboard::Mutex& Lock() {
    return mutex_;
  }

private:
  const SbTime check_interval_ { get_check_interval() };

  SbThread thread_;
  bool running_ { true };
  std::vector<HangMonitor*> monitors_;

  ::starboard::Mutex mutex_;
  ::starboard::ConditionVariable condition_ { mutex_ };
};

SB_ONCE_INITIALIZE_FUNCTION(HangDetector, GetHangDetector);

}  // namespace

HangMonitor::HangMonitor(std::string name)
  : name_(std::move(name))
  , expiration_time_(SbTimeGetMonotonicNow() + GetHangDetector()->GetCheckInterval()) {
  GetHangDetector()->AddMonitor(this);
}

HangMonitor::~HangMonitor() {
  GetHangDetector()->RemoveMonitor(this);
}

const std::string& HangMonitor::Name() const {
  return name_;
}

SbTimeMonotonic HangMonitor::GetExpirationTime() const {
  return expiration_time_;
}

SbTime HangMonitor::GetResetInterval() const {
  return GetHangDetector()->GetCheckInterval() / 2;
}

int HangMonitor::IncExpirationCount() {
  return ++expiration_count_;
}

void HangMonitor::Reset() {
  auto &hang_detector = *GetHangDetector();
  ::starboard::ScopedLock lock(hang_detector.Lock());
  expiration_time_ = SbTimeGetMonotonicNow() + hang_detector.GetCheckInterval();
  expiration_count_ = 0;
}

}  // namespace shared
}  // namespace rdk
}  // namespace starboard
}  // namespace third_party
