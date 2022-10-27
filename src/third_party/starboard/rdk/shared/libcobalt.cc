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

#include "third_party/starboard/rdk/shared/libcobalt.h"

#include "starboard/common/condition_variable.h"
#include "starboard/common/mutex.h"
#include "starboard/common/semaphore.h"
#include "starboard/once.h"

#include "third_party/starboard/rdk/shared/application_rdk.h"

using namespace third_party::starboard::rdk::shared;

namespace
{

struct APIContext
{
  APIContext()
    : mutex_()
    , condition_(mutex_)
  { }

  void OnInitialize()
  {
    starboard::ScopedLock lock(mutex_);
    running_ = (nullptr != Application::Get());
    condition_.Broadcast();
  }

  void OnTeardown()
  {
    starboard::ScopedLock lock(mutex_);
    running_ = false;
  }

  void SendLink(const char* link)
  {
    starboard::ScopedLock lock(mutex_);
    WaitForApp(lock);
    Application::Get()->Link(link);
  }

  bool IsResumed() {
    return Application::Get()->IsResumed();
  }

  void RequestSuspend() {
    starboard::ScopedLock lock(mutex_);
    WaitForApp(lock);
    Application::Get()->SendBlurEvent();
    Application::Get()->SendConcealEvent();
    Application::Get()->SendFreezeEvent();
  }

  void RequestResume() {
    starboard::ScopedLock lock(mutex_);
    WaitForApp(lock);
    bool focusstatus= Application::Get()->GetFocusStatus();
    Application::Get()->SendUnfreezeEvent();
    Application::Get()->SendRevealEvent();
    if (focusstatus)
    {
      Application::Get()->SendFocusEvent();
    }
  }

  void RequestPause() {
    starboard::ScopedLock lock(mutex_);
    WaitForApp(lock);
    starboard::Semaphore sem;
    Application::Get()->Blur(
      &sem,
      [](void* ctx) {
        reinterpret_cast<starboard::Semaphore*>(ctx)->Put();
      });
    sem.Take();
  }

  void RequestUnpause() {
    starboard::ScopedLock lock(mutex_);
    WaitForApp(lock);
    starboard::Semaphore sem;
    Application::Get()->Focus(
      &sem,
      [](void* ctx) {
        reinterpret_cast<starboard::Semaphore*>(ctx)->Put();
      });
    sem.Take();
  }

  void RequestStop()
  {
    starboard::ScopedLock lock(mutex_);
    if (running_)
        Application::Get()->Stop(0);
  }

private:
  void WaitForApp(starboard::ScopedLock &)
  {
    while ( running_ == false )
      condition_.Wait();
  }

  bool running_ { false };
  starboard::Mutex mutex_;
  starboard::ConditionVariable condition_;
};

SB_ONCE_INITIALIZE_FUNCTION(APIContext, GetContext);

}  // namespace

namespace third_party {
namespace starboard {
namespace rdk {
namespace shared {
namespace libcobalt_api {

void Initialize()
{
  GetContext()->OnInitialize();
}

void Teardown()
{
  GetContext()->OnTeardown();
}

}  // namespace libcobalt_api
}  // namespace shared
}  // namespace rdk
}  // namespace starboard
}  // namespace third_party

extern "C" {

void SbRdkHandleDeepLink(const char* link) {
  GetContext()->SendLink(link);
}

void SbRdkSuspend() {
  GetContext()->RequestSuspend();
}

void SbRdkResume() {
  GetContext()->RequestResume();
}

void SbRdkPause() {
  GetContext()->RequestPause();
}

void SbRdkUnpause() {
  GetContext()->RequestUnpause();
}

void SbRdkQuit() {
  GetContext()->RequestStop();
}

void SbRdkSetSetting(const char* key, const char* json) {
}

int SbRdkGetSetting(const char* key, char** out_json) {
    return -1;
}

bool SbRdkIsResumed() {
  return GetContext()->IsResumed();
}

static libCobaltCallback cobalt_plugin_callback = nullptr;

void SbRdkRegisterNotify(libCobaltCallback callback) {
  cobalt_plugin_callback = callback;
}

void cobalt_oem_notify_suspend(int reason, void* param) {
  if (cobalt_plugin_callback)
    cobalt_plugin_callback(reason, param);
  else
    printf("%s %s(%d) ERROR cobalt_plugin_callback is empty\n", __FILE__, __func__, __LINE__);
}

}  // extern "C"
