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
#if defined(HAS_OCDM)
#include "third_party/starboard/rdk/shared/drm/drm_system_ocdm.h"

#include <dlfcn.h>
#include <mutex>
#include <gst/gst.h>

#include "starboard/common/mutex.h"
#include "starboard/shared/starboard/thread_checker.h"

#include "opencdm/open_cdm.h"
#include "opencdm/open_cdm_adapter.h"

#include "third_party/starboard/rdk/shared/log_override.h"

namespace third_party {
namespace starboard {
namespace rdk {
namespace shared {
namespace drm {
namespace {

struct OcdmSessionDeleter {
  void operator()(OpenCDMSession* session) {
    opencdm_destruct_session(session);
  }
};

using ScopedOcdmSession = std::unique_ptr<OpenCDMSession, OcdmSessionDeleter>;

using OcdmGstSessionDecryptExFn =
  OpenCDMError(*)(struct OpenCDMSession*, GstBuffer*, GstBuffer*, const uint32_t, GstBuffer*, GstBuffer*, uint32_t, GstCaps*);

static OcdmGstSessionDecryptExFn g_ocdmGstSessionDecryptEx { nullptr };

}  // namespace

namespace session {

SbDrmKeyStatus KeyStatus2DrmKeyStatus(KeyStatus status) {
  switch (status) {
    case Usable:
      return kSbDrmKeyStatusUsable;
    case Expired:
      return kSbDrmKeyStatusExpired;
    case Released:
      return kSbDrmKeyStatusReleased;
    case OutputRestricted:
      return kSbDrmKeyStatusRestricted;
    case OutputDownscaled:
      return kSbDrmKeyStatusDownscaled;
    case StatusPending:
      return kSbDrmKeyStatusPending;
    default:
    case InternalError:
      return kSbDrmKeyStatusError;
  }
}

class Session {
 public:
  Session(Session&) = delete;
  Session& operator=(Session&) = delete;

  Session(
      DrmSystemOcdm* drm_system,
      OpenCDMSystem* ocdm_system,
      void* context,
      const SbDrmSessionUpdateRequestFunc& session_update_request_callback,
      const SbDrmSessionUpdatedFunc& session_updated_callback,
      const SbDrmSessionKeyStatusesChangedFunc& key_statuses_changed_callback,
      const SbDrmSessionClosedFunc session_closed_callback);
  ~Session();
  void Close();
  void GenerateChallenge(const std::string& type,
                         const void* initialization_data,
                         int initialization_data_size,
                         int ticket);
  void Update(const void* key, int key_size, int ticket);
  std::string Id() const { return id_; }
  OpenCDMSession* OcdmSession() const { return session_.get(); }
  int GetSessionFrameWidth() const {return frame_width_; }
  int GetSessionFrameHeight() const {return frame_height_; }
  void SetSessionFrameWidth(int width)  { frame_width_ = width; }
  void SetSessionFrameHeight(int height) { frame_height_ = height; }

 private:
  static void OnProcessChallenge(OpenCDMSession* session,
                                 void* user_data,
                                 const char url[],
                                 const uint8_t challenge[],
                                 const uint16_t challenge_length);
  static void ProcessChallenge(Session* session,
                               int ticket,
                               std::string&& id,
                               std::string&& url,
                               std::string&& challenge);
  static void OnKeyUpdated(struct OpenCDMSession* session,
                           void* user_data,
                           const uint8_t key_id[],
                           const uint8_t length);
  static void OnAllKeysUpdated(const struct OpenCDMSession* session,
                               void* user_data);
  static void OnError(struct OpenCDMSession* session,
                      void* user_data,
                      const char message[]);

  OpenCDMSessionCallbacks session_callbacks_ = {
      &Session::OnProcessChallenge, &Session::OnKeyUpdated, &Session::OnError,
      &Session::OnAllKeysUpdated};

  enum class Operation {
    kNone,
    kGenrateChallenge,
    kUpdate,
  };

  ::starboard::shared::starboard::ThreadChecker thread_checker_;
  Operation operation_{Operation::kNone};
  int ticket_{0};
  DrmSystemOcdm* drm_system_;
  OpenCDMSystem* ocdm_system_;
  ScopedOcdmSession session_;
  void* context_;
  const SbDrmSessionUpdateRequestFunc session_update_request_callback_;
  const SbDrmSessionUpdatedFunc session_updated_callback_;
  const SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback_;
  const SbDrmSessionClosedFunc session_closed_callback_;
  ::starboard::Mutex mutex_;
  std::string last_challenge_;
  std::string last_challenge_url_;
  std::string id_;
  int frame_width_{0};
  int frame_height_{0};
};

Session::Session(
    DrmSystemOcdm* drm_system,
    OpenCDMSystem* ocdm_system,
    void* context,
    const SbDrmSessionUpdateRequestFunc& session_update_request_callback,
    const SbDrmSessionUpdatedFunc& session_updated_callback,
    const SbDrmSessionKeyStatusesChangedFunc& key_statuses_changed_callback,
    const SbDrmSessionClosedFunc session_closed_callback)
    : drm_system_(drm_system),
      ocdm_system_(ocdm_system),
      context_(context),
      session_update_request_callback_(session_update_request_callback),
      session_updated_callback_(session_updated_callback),
      key_statuses_changed_callback_(key_statuses_changed_callback),
      session_closed_callback_(session_closed_callback) {}

Session::~Session() {
  Close();
}

void Session::Close() {
  SB_DCHECK(thread_checker_.CalledOnValidThread());
  if (session_) {
    opencdm_session_close(session_.get());
    session_ = nullptr;
  }

  auto id = Id();
  if (!id.empty())
    session_closed_callback_(drm_system_, context_, id.c_str(), id.size());
  else
    SB_LOG(WARNING) << "Closing ivalid session ?";

  {
    ::starboard::ScopedLock lock(mutex_);
    ticket_ = kSbDrmTicketInvalid;
    operation_ = Operation::kNone;
    id_.clear();
  }
}

void Session::GenerateChallenge(const std::string& type,
                                const void* initialization_data,
                                int initialization_data_size,
                                int ticket) {
  SB_DCHECK(thread_checker_.CalledOnValidThread());
  SB_LOG(INFO) << "Generating challenge";
  {
    ::starboard::ScopedLock lock(mutex_);
    ticket_ = ticket;
    operation_ = Operation::kGenrateChallenge;
  }
  OpenCDMSession* session = nullptr;
  if (opencdm_construct_session(
          ocdm_system_, Temporary, type.c_str(),
          reinterpret_cast<const uint8_t*>(initialization_data),
          initialization_data_size, nullptr, 0, &session_callbacks_, this,
          &session) != ERROR_NONE ||
      !session) {
    session_update_request_callback_(drm_system_, context_, ticket,
                                     kSbDrmStatusUnknownError,
                                     kSbDrmSessionRequestTypeLicenseRequest,
                                     nullptr, nullptr, 0, nullptr, 0, nullptr);
    return;
  }

  session_.reset(session);
  std::string challenge;
  std::string url;
  std::string id;
  {
    ::starboard::ScopedLock lock(mutex_);
    id_ = opencdm_session_id(session_.get());
    id = id_;
    challenge.swap(last_challenge_);
    url.swap(last_challenge_url_);
  }

  if (!challenge.empty()) {
    Session::ProcessChallenge(this, ticket, std::move(id), std::move(url),
                              std::move(challenge));
  }
}

void Session::Update(const void* key, int key_size, int ticket) {
  SB_DCHECK(thread_checker_.CalledOnValidThread());
  auto id = Id();
  SB_DCHECK(!id.empty());
  {
    ::starboard::ScopedLock lock(mutex_);
    SB_LOG(INFO) << "Updating session " << id << " ticket " << ticket;
    ticket_ = ticket;
    operation_ = Operation::kUpdate;
  }
  if (opencdm_session_update(session_.get(), static_cast<const uint8_t*>(key),
                             key_size) != ERROR_NONE) {
    session_updated_callback_(drm_system_, context_, ticket,
                              kSbDrmStatusUnknownError, nullptr, id.c_str(),
                              id.size());
  }
  SB_LOG(INFO) << "sent update message to widevine OCDM server";
}

// static
void Session::OnProcessChallenge(OpenCDMSession* ocdm_session,
                                 void* user_data,
                                 const char url[],
                                 const uint8_t challenge[],
                                 const uint16_t challenge_length) {
  Session* session = static_cast<Session*>(user_data);
  std::string id;
  int ticket;
  {
    ::starboard::ScopedLock lock(session->mutex_);
    id = session->Id();
    if (id.empty()) {
      session->last_challenge_url_ = url;
      session->last_challenge_ = {reinterpret_cast<char const*>(challenge),
                                  challenge_length};
      return;
    }

    session->operation_ = Operation::kNone;
    ticket = session->ticket_;
    session->ticket_ = kSbDrmTicketInvalid;
  }

  std::string challenge_str = {reinterpret_cast<const char*>(challenge),
                               challenge_length};
  Session::ProcessChallenge(session, ticket, std::move(id), {url},
                            std::move(challenge_str));
}

// static
void Session::ProcessChallenge(Session* session,
                               int ticket,
                               std::string&& id,
                               std::string&& url,
                               std::string&& challenge) {
  SB_DCHECK(!id.empty() && !challenge.empty());

  size_t type_position = challenge.find(":Type:");
  std::string request_type = {
      challenge.c_str(),
      type_position != std::string::npos ? type_position : 0};

  unsigned offset = 0u;
  if (!request_type.empty() && request_type.length() != challenge.length())
    offset = type_position + 6;

  SbDrmSessionRequestType message_type = kSbDrmSessionRequestTypeLicenseRequest;
  if (request_type.length() == 1)
    message_type =
        static_cast<SbDrmSessionRequestType>(std::stoi(request_type));

  challenge = {challenge.c_str() + offset, challenge.size() - offset};

  SB_LOG(INFO) << "Process challenge for " << id << " type " << request_type;
  session->session_update_request_callback_(
      session->drm_system_, session->context_, ticket, kSbDrmStatusSuccess,
      message_type, "", id.c_str(), id.size(), challenge.c_str(),
      challenge.size(), url.c_str());
}

// static
void Session::OnKeyUpdated(struct OpenCDMSession* /*ocdm_session*/,
                           void* user_data,
                           const uint8_t key_id[],
                           const uint8_t length) {
  Session* session = static_cast<Session*>(user_data);
  std::string id;
  {
    ::starboard::ScopedLock lock(session->mutex_);
    id = session->Id();
  }
  if (id.empty()) {
    SB_LOG(WARNING) << "Updating closed session ?";
    return;
  }

  auto status = opencdm_session_status(session->session_.get(), key_id, length);
  SB_LOG(INFO) << "session-id " << id << " from OCDM server, save key info to session, not call cobalt callback, key-id "\
      << DrmSystemOcdm::hex2string(key_id, length).c_str() << \
      " status " << DrmSystemOcdm::keyStatusToString(KeyStatus2DrmKeyStatus(status));
  SbDrmKeyId drm_key_id;
  std::copy_n(key_id, length, drm_key_id.identifier);
  drm_key_id.identifier_size = length;
  session->drm_system_->OnKeyUpdated(id, std::move(drm_key_id),
                                     KeyStatus2DrmKeyStatus(status));
}

// static
void Session::OnAllKeysUpdated(const struct OpenCDMSession* /*ocdm_session*/,
                               void* user_data) {
  Session* session = static_cast<Session*>(user_data);
  int ticket;
  std::string id;
  {
    ::starboard::ScopedLock lock(session->mutex_);
    id = session->Id();
    session->operation_ = Operation::kNone;
    ticket = session->ticket_;
    session->ticket_ = kSbDrmTicketInvalid;
  }
  if (id.empty()) {
    SB_LOG(WARNING) << "Updating closed session ?";
    return;
  }

  session->session_updated_callback_(session->drm_system_, session->context_,
                                     ticket, kSbDrmStatusSuccess, nullptr,
                                     id.c_str(), id.size());
  session->drm_system_->OnAllKeysUpdated();

  SB_LOG(INFO) << "from OCDM server, updating all the keys status and inovke cobalt callback " << " session-id " << id;
  auto session_keys = session->drm_system_->GetSessionKeys(id);
  std::vector<SbDrmKeyId> keys;
  std::vector<SbDrmKeyStatus> statuses;
  for (auto& session_key : session_keys) {
    keys.push_back(session_key.key);
    statuses.push_back(session_key.status);
  }
  session->key_statuses_changed_callback_(
      session->drm_system_, session->context_, id.c_str(), id.size(),
      session_keys.size(), keys.data(), statuses.data());

  SB_LOG(INFO) << "from OCDM server, all keys status update ended, session-id " << id;
}

// static
void Session::OnError(struct OpenCDMSession* /*ocdm_session*/,
                      void* user_data,
                      const char message[]) {
  Session* session = static_cast<Session*>(user_data);
  int ticket;
  std::string id;
  Operation operation;
  {
    ::starboard::ScopedLock lock(session->mutex_);
    operation = session->operation_;
    session->operation_ = Operation::kNone;
    ticket = session->ticket_;
    session->ticket_ = kSbDrmTicketInvalid;
    id = session->Id();
  }
  SB_LOG(ERROR) << "DRM error: " << message << ", session " << id;
  switch (operation) {
    case Operation::kGenrateChallenge:
      session->session_update_request_callback_(
          session->drm_system_, session->context_, ticket,
          kSbDrmStatusUnknownError, kSbDrmSessionRequestTypeLicenseRequest,
          nullptr, nullptr, 0, nullptr, 0, nullptr);
      break;
    case Operation::kUpdate:
      session->session_updated_callback_(
          session->drm_system_, session->context_, ticket,
          kSbDrmStatusUnknownError, nullptr, id.c_str(), id.size());
      break;
    case Operation::kNone:
      break;
    default:
      SB_NOTREACHED();
      break;
  }
}

}  // namespace session

using session::Session;

DrmSystemOcdm::DrmSystemOcdm(
    const char* key_system,
    void* context,
    SbDrmSessionUpdateRequestFunc session_update_request_callback,
    SbDrmSessionUpdatedFunc session_updated_callback,
    SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback,
    SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback,
    SbDrmSessionClosedFunc session_closed_callback)
    : key_system_(key_system),
      context_(context),
      session_update_request_callback_(session_update_request_callback),
      session_updated_callback_(session_updated_callback),
      key_statuses_changed_callback_(key_statuses_changed_callback),
      server_certificate_updated_callback_(server_certificate_updated_callback),
      session_closed_callback_(session_closed_callback) {
  SB_LOG(INFO) << "Create DRM system ";
  ocdm_system_ = opencdm_create_system(key_system_.c_str());

  static std::once_flag flag;
  /*
  std::call_once(flag, [](){
    g_ocdmGstSessionDecryptEx = (OcdmGstSessionDecryptExFn)dlsym(RTLD_DEFAULT, "opencdm_gstreamer_session_decrypt_ex");
    if (g_ocdmGstSessionDecryptEx) {
      SB_LOG(INFO) << "Has opencdm_gstreamer_session_decrypt_ex";
    } else {
      SB_LOG(INFO) << "No opencdm_gstreamer_session_decrypt_ex. Fallback to opencdm_gstreamer_session_decrypt.";
    }
  });
  */
}

DrmSystemOcdm::~DrmSystemOcdm() {
  {
    ::starboard::ScopedLock lock(mutex_);
    if (event_id_ != kSbEventIdInvalid)
      SbEventCancel(event_id_);
  }
  opencdm_destruct_system(ocdm_system_);
}

/**
 class DrmSystemOcdm use below map structure to store key-id/key-status, map key is session-id
 each session corresponding to a license, contain multiple content key(audio/video, renewal key)
_______________________________________________________
|session-id-1| key-id/status |key-id/status | ......
|____________|_______________|______________|__________
|session-id-2| key-id/status |key-id/status | ......
|____________|_______________|______________|__________
|session-id-3| key-id/status |key-id/status | ......
|____________|_______________|______________|__________
|session-id-4| key-id/status |key-id/status | ......
|____________|_______________|______________|__________
**/
SbDrmKeyStatus DrmSystemOcdm::GetKeyStatus(const uint8_t * key, uint32_t key_size){
  SbDrmKeyId drm_key_id;
  SbDrmKeyStatus status = kSbDrmKeyStatusError;

  std::copy_n(key, key_size, drm_key_id.identifier);
  drm_key_id.identifier_size = key_size;

  for (auto & session : sessions_){
      auto session_key = session_keys_.find(session->Id());
      if (session_key != session_keys_.end()) {
          auto key_entry = std::find_if(
                  session_key->second.begin(), session_key->second.end(),
                  [&drm_key_id](const KeyWithStatus& key_with_status) {
                  return memcmp(drm_key_id.identifier, key_with_status.key.identifier,
                          std::min(key_with_status.key.identifier_size,
                              drm_key_id.identifier_size)) == 0;
                  });
          if (key_entry != session_key->second.end()) {
              status = key_entry->status;
          }
      }
  }

  return status;
}

// static
bool DrmSystemOcdm::IsKeySystemSupported(const char* key_system,
                                         const char* mime_type) {
  return opencdm_is_type_supported(key_system, mime_type) == ERROR_NONE;
}

void DrmSystemOcdm::GenerateSessionUpdateRequest(
    int ticket,
    const char* type,
    const void* initialization_data,
    int initialization_data_size) {
  SB_LOG(INFO) << "Generate challenge type: " << type;
  std::unique_ptr<Session> session(
      new Session(this, ocdm_system_, context_,
                  session_update_request_callback_, session_updated_callback_,
                  key_statuses_changed_callback_, session_closed_callback_));
  session->GenerateChallenge(type, initialization_data,
                             initialization_data_size, ticket);
  sessions_.push_back(std::move(session));
}

void DrmSystemOcdm::UpdateSession(int ticket,
                                  const void* key,
                                  int key_size,
                                  const void* session_id,
                                  int session_id_size) {
  std::string id = {static_cast<const char*>(session_id), session_id_size};
  SB_LOG(INFO) << "Update: " << id << " ticket " << ticket;
  auto* session = GetSessionById(id);
  if (session)
    session->Update(key, key_size, ticket);
}

void DrmSystemOcdm::CloseSession(const void* session_id, int session_id_size) {
  std::string id = {static_cast<const char*>(session_id), session_id_size};
  SB_LOG(INFO) << "Close: " << id;
  auto* session = GetSessionById(id);
  if (session)
    session->Close();
}

void DrmSystemOcdm::UpdateServerCertificate(int ticket,
                                            const void* certificate,
                                            int certificate_size) {
  auto status = opencdm_system_set_server_certificate(
      ocdm_system_, static_cast<const uint8_t*>(certificate), certificate_size);

  server_certificate_updated_callback_(
      this, context_, ticket,
      status == ERROR_NONE ? kSbDrmStatusSuccess : kSbDrmStatusUnknownError,
      "Error");
}

SbDrmSystemPrivate::DecryptStatus DrmSystemOcdm::Decrypt(InputBuffer* buffer) {
  SB_NOTREACHED();
  return kFailure;
}

/*********
 * frame width and height is a session based setting, save width/height into session class,
 * only if width or height changed, call OCDM setting function
 ********/
void DrmSystemOcdm::SetVideoResolution(const std::string & session_id, uint32_t width, uint32_t height){
  OpenCDMError ret = ERROR_NONE;
  auto iter = std::find_if(
      sessions_.begin(), sessions_.end(),
      [&session_id](const std::unique_ptr<Session>& s) { return session_id == s->Id(); });

  if (iter != sessions_.end()){
      SB_DCHECK(session);

      if (width > 0 && height > 0 && ((width != iter->get()->GetSessionFrameWidth()) || (height != iter->get()->GetSessionFrameHeight()))) {
          char param[32];
          sprintf(param, "%d,%d", width, height);
          if ((ret = opencdm_session_set_parameter(iter->get()->OcdmSession(), std::string("RESOLUTION"), std::string(param))) == ERROR_NONE){
              SB_LOG(INFO) << "set resolution width: " << width << " height:" << height << " session id " << iter->get()->Id();
              iter->get()->SetSessionFrameWidth(width);
              iter->get()->SetSessionFrameHeight(height);
          }else{
              SB_LOG(ERROR) << "set session resolution error ret " << ret;
          }
      }
  }else{
      SB_LOG(ERROR) << "set session resolution error, can not find session with id " << session_id;
  }
}

Session* DrmSystemOcdm::GetSessionById(const std::string& id) const {
  auto iter = std::find_if(
      sessions_.begin(), sessions_.end(),
      [&id](const std::unique_ptr<Session>& s) { return id == s->Id(); });

  if (iter != sessions_.end())
    return iter->get();

  return nullptr;
}

void DrmSystemOcdm::AddObserver(DrmSystemOcdm::Observer* obs) {
  ::starboard::ScopedLock lock(mutex_);
  observers_.push_back(obs);
}

void DrmSystemOcdm::RemoveObserver(DrmSystemOcdm::Observer* obs) {
  ::starboard::ScopedLock lock(mutex_);
  auto found = std::find(observers_.begin(), observers_.end(), obs);
  SB_DCHECK(found != observers_.end());
  observers_.erase(found);
}

void DrmSystemOcdm::OnKeyUpdated(const std::string& session_id,
                                 SbDrmKeyId&& key_id,
                                 SbDrmKeyStatus status) {
  ::starboard::ScopedLock lock(mutex_);
  auto session_key = session_keys_.find(session_id);
  KeyWithStatus key_with_status;
  key_with_status.key = std::move(key_id);
  key_with_status.status = status;
  if (session_key == session_keys_.end()) {
    session_keys_[session_id].emplace_back(std::move(key_with_status));
  } else {
    auto key_entry = std::find_if(
        session_key->second.begin(), session_key->second.end(),
        [&key_id](const KeyWithStatus& key_with_status) {
          return memcmp(key_id.identifier, key_with_status.key.identifier,
                        std::min(key_with_status.key.identifier_size,
                                 key_id.identifier_size)) == 0;
        });
    if (key_entry != session_key->second.end()) {
      key_entry->status = status;
    } else {
      session_key->second.emplace_back(std::move(key_with_status));
    }
  }
}

void DrmSystemOcdm::OnAllKeysUpdated() {
  ::starboard::ScopedLock lock(mutex_);
  cached_ready_keys_.clear();
  if (event_id_ != kSbEventIdInvalid)
    SbEventCancel(event_id_);
  event_id_ = SbEventSchedule(
      [](void* data) {
        DrmSystemOcdm* self = reinterpret_cast<DrmSystemOcdm*>(data);
        self->AnnounceKeys();
      },
      this, 0);
}

std::set<std::string> DrmSystemOcdm::GetReadyKeysUnlocked() const {
  ::starboard::ScopedLock lock(mutex_);
  if (cached_ready_keys_.empty()) {
    for (auto& session_key : session_keys_) {
      for (auto& key_with_status : session_key.second) {
        cached_ready_keys_.emplace(std::string{
            reinterpret_cast<const char*>(key_with_status.key.identifier),
            key_with_status.key.identifier_size});
      }
    }
  }

  return cached_ready_keys_;
}

std::set<std::string> DrmSystemOcdm::GetReadyKeys() const {
  return GetReadyKeysUnlocked();
}

DrmSystemOcdm::KeysWithStatus DrmSystemOcdm::GetSessionKeys(
    const std::string& session_id) const {
  auto session_key = session_keys_.find(session_id);
  return session_key != session_keys_.end() ? session_key->second
                                            : DrmSystemOcdm::KeysWithStatus{};
}

void DrmSystemOcdm::AnnounceKeys() {
  auto ready_keys = GetReadyKeysUnlocked();
  for (auto* observer : observers_) {
    for (auto& key : ready_keys) {
      observer->OnKeyReady(reinterpret_cast<const uint8_t*>(key.c_str()),
                           key.size());
    }
  }
  event_id_ = kSbEventIdInvalid;
}

std::string DrmSystemOcdm::SessionIdByKeyId(const uint8_t* key,
                                            uint8_t key_len) {
  ScopedOcdmSession session{
      opencdm_get_system_session(ocdm_system_, key, key_len, 0)};
  return session ? opencdm_session_id(session.get()) : std::string{};
}

bool DrmSystemOcdm::Decrypt(const std::string& id,
                            _GstBuffer* buffer,
                            _GstBuffer* sub_sample,
                            uint32_t sub_sample_count,
                            _GstBuffer* iv,
                            _GstBuffer* key,
                            _GstCaps* caps) {
  session::Session* session = GetSessionById(id);
  if (session == nullptr)
  {
    SB_LOG(ERROR) << "GetSessionById nullptr! " ;
    return false;
  }
  SB_DCHECK(session);
  if (g_ocdmGstSessionDecryptEx != nullptr) {
    return g_ocdmGstSessionDecryptEx(session->OcdmSession(), buffer,
                                     sub_sample, sub_sample_count, iv,
                                     key, 0, caps) == ERROR_NONE;
  }
  return opencdm_gstreamer_session_decrypt(session->OcdmSession(), buffer,
                                           sub_sample, sub_sample_count, iv,
                                           key, 0) == ERROR_NONE;
}

bool DrmSystemOcdm::Decrypt(const std::string& id,
        _GstBuffer* buffer,
        _GstBuffer* sub_sample,
        uint32_t sub_sample_count,
        _GstBuffer* iv,
        _GstBuffer* key_id,
        _GstCaps* caps,
        const SbDrmEncryptionScheme & encryption_scheme,
        const SbDrmEncryptionPattern & encryption_pattern){
  session::Session* session = GetSessionById(id);
  if (session == nullptr)
  {
    SB_LOG(ERROR) << "GetSessionById nullptr! " ;
    return false;
  }
  SB_DCHECK(session);
  if (g_ocdmGstSessionDecryptEx != nullptr) {
    return g_ocdmGstSessionDecryptEx(session->OcdmSession(), buffer,
                                     sub_sample, sub_sample_count, iv,
                                     key_id, 0, caps) == ERROR_NONE;
  }
#ifndef USED_SVP_EXT
  return opencdm_gstreamer_session_decrypt_new(session->OcdmSession(), buffer,
                                           sub_sample, sub_sample_count, iv,
                                           key_id, 0, encryption_pattern.crypt_byte_block,
                                           encryption_pattern.skip_byte_block,
                                           encryption_scheme) == ERROR_NONE;
#else
  return opencdm_gstreamer_session_decrypt_ex_new(session->OcdmSession(),
                                          buffer,
                                          sub_sample,
                                          sub_sample_count,
                                          iv,
                                          key_id,
                                          0,
                                          encryption_pattern.crypt_byte_block,
                                          encryption_pattern.skip_byte_block,
                                          encryption_scheme,
                                          caps) == ERROR_NONE;
#endif
}

const void* DrmSystemOcdm::GetMetrics(int* size) {
    OpenCDMError result =  opencdm_get_metrics(ocdm_system_, metrics_data_);
    if (ERROR_NONE == result) {
        *size = metrics_data_.size();
        return (void*)metrics_data_.data();
    }
    return nullptr;
}

}  // namespace drm
}  // namespace shared
}  // namespace rdk
}  // namespace starboard
}  // namespace third_party

#else  // defined(HAS_OCDM)

#include "third_party/starboard/rdk/shared/drm/drm_system_ocdm.h"

namespace third_party {
namespace starboard {
namespace rdk {
namespace shared {
namespace drm {

namespace session {
struct Session {};
}
using session::Session;

DrmSystemOcdm::DrmSystemOcdm(
    const char* key_system,
    void* context,
    SbDrmSessionUpdateRequestFunc session_update_request_callback,
    SbDrmSessionUpdatedFunc session_updated_callback,
    SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback,
    SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback,
    SbDrmSessionClosedFunc session_closed_callback)
    : key_system_(key_system),
      context_(context),
      session_update_request_callback_(session_update_request_callback),
      session_updated_callback_(session_updated_callback),
      key_statuses_changed_callback_(key_statuses_changed_callback),
      server_certificate_updated_callback_(server_certificate_updated_callback),
      session_closed_callback_(session_closed_callback) {
}

DrmSystemOcdm::~DrmSystemOcdm() {
}

// static
bool DrmSystemOcdm::IsKeySystemSupported(const char* key_system,
                                         const char* mime_type) {
  return false;
}

void DrmSystemOcdm::GenerateSessionUpdateRequest(
    int ticket,
    const char* type,
    const void* initialization_data,
    int initialization_data_size) {
}

void DrmSystemOcdm::UpdateSession(int ticket,
                                  const void* key,
                                  int key_size,
                                  const void* session_id,
                                  int session_id_size) {
}

void DrmSystemOcdm::CloseSession(const void* session_id, int session_id_size) {
}

void DrmSystemOcdm::UpdateServerCertificate(int ticket,
                                            const void* certificate,
                                            int certificate_size) {
}

SbDrmSystemPrivate::DecryptStatus DrmSystemOcdm::Decrypt(InputBuffer* buffer) {
  return kFailure;
}

Session* DrmSystemOcdm::GetSessionById(const std::string& id) const {
  return nullptr;
}

void DrmSystemOcdm::AddObserver(DrmSystemOcdm::Observer* obs) {
}

void DrmSystemOcdm::RemoveObserver(DrmSystemOcdm::Observer* obs) {
}

void DrmSystemOcdm::OnKeyUpdated(const std::string& session_id,
                                 SbDrmKeyId&& key_id,
                                 SbDrmKeyStatus status) {
}

void DrmSystemOcdm::OnAllKeysUpdated() {
}

std::set<std::string> DrmSystemOcdm::GetReadyKeysUnlocked() const {
  return {};
}

std::set<std::string> DrmSystemOcdm::GetReadyKeys() const {
  return GetReadyKeysUnlocked();
}

DrmSystemOcdm::KeysWithStatus DrmSystemOcdm::GetSessionKeys(
    const std::string& session_id) const {
  return DrmSystemOcdm::KeysWithStatus{};
}

void DrmSystemOcdm::AnnounceKeys() {
}

std::string DrmSystemOcdm::SessionIdByKeyId(const uint8_t* key,
                                            uint8_t key_len) {
  return std::string{};
}

bool DrmSystemOcdm::Decrypt(const std::string& id,
                            _GstBuffer* buffer,
                            _GstBuffer* sub_sample,
                            uint32_t sub_sample_count,
                            _GstBuffer* iv,
                            _GstBuffer* key,
                            _GstCaps* caps) {
  return false;
}

const void* DrmSystemOcdm::GetMetrics(int* size) {
    return nullptr;
}

}  // namespace drm
}  // namespace shared
}  // namespace rdk
}  // namespace starboard
}  // namespace third_party

#endif  // defined(HAS_OCDM)
