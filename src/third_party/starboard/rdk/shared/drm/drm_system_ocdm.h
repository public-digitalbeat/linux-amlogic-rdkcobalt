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
#ifndef THIRD_PARTY_STARBOARD_RDK_SHARED_DRM_DRM_SYSTEM_OCDM_H_
#define THIRD_PARTY_STARBOARD_RDK_SHARED_DRM_DRM_SYSTEM_OCDM_H_

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "starboard/common/mutex.h"
#include "starboard/event.h"
#include "starboard/shared/starboard/drm/drm_system_internal.h"
#include "starboard/shared/starboard/thread_checker.h"
#include "starboard/thread.h"

// For new DRM SVP-EXT, ocdm allocate the secmem
#define USED_SVP_EXT 1

struct _GstCaps;
struct _GstBuffer;
struct OpenCDMSystem;

namespace third_party {
namespace starboard {
namespace rdk {
namespace shared {
namespace drm {

namespace session {
class Session;
}

class DrmSystemOcdm : public SbDrmSystemPrivate {
 public:
  class Observer {
   public:
    virtual ~Observer() {}
    virtual void OnKeyReady(const uint8_t* key, size_t key_len) = 0;
  };

  struct KeyWithStatus {
    SbDrmKeyId key;
    SbDrmKeyStatus status;
  };

  using KeysWithStatus = std::vector<KeyWithStatus>;

  DrmSystemOcdm(
      const char* key_system,
      void* context,
      SbDrmSessionUpdateRequestFunc update_request_callback,
      SbDrmSessionUpdatedFunc session_updated_callback,
      SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback,
      SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback,
      SbDrmSessionClosedFunc session_closed_callback);

  ~DrmSystemOcdm() override;

  SbDrmKeyStatus GetKeyStatus(const uint8_t * key, uint32_t key_size);
  static bool IsKeySystemSupported(const char* key_system,
                                   const char* mime_type);

  // SbDrmSystemPrivate
  void GenerateSessionUpdateRequest(int ticket,
                                    const char* type,
                                    const void* initialization_data,
                                    int initialization_data_size) override;
  void CloseSession(const void* session_id, int session_id_size) override;
  void UpdateSession(int ticket,
                     const void* key,
                     int key_size,
                     const void* session_id,
                     int session_id_size) override;
  DecryptStatus Decrypt(InputBuffer* buffer) override;
  bool IsServerCertificateUpdatable() override { return false; }
  void UpdateServerCertificate(int ticket,
                               const void* certificate,
                               int certificate_size) override;

  const void* GetMetrics(int* size) override;

  void SetVideoResolution(const std::string & session_id, uint32_t width, uint32_t height);
  void AddObserver(Observer* obs);
  void RemoveObserver(Observer* obs);
  void OnKeyUpdated(const std::string& session_id,
                    SbDrmKeyId&& key_id,
                    SbDrmKeyStatus status);
  void OnAllKeysUpdated();
  std::string SessionIdByKeyId(const uint8_t* key, uint8_t key_len);
  bool Decrypt(const std::string& id,
               _GstBuffer* buffer,
               _GstBuffer* sub_sample,
               uint32_t sub_sample_count,
               _GstBuffer* iv,
               _GstBuffer* key_id,
               _GstCaps* caps);

  // The encryption pattern of this sample.
  bool Decrypt(const std::string& id,
               _GstBuffer* buffer,
               _GstBuffer* sub_sample,
               uint32_t sub_sample_count,
               _GstBuffer* iv,
               _GstBuffer* key_id,
               _GstCaps* caps,
               const SbDrmEncryptionScheme & encryption_scheme,
               const SbDrmEncryptionPattern & encryption_pattern);

  std::set<std::string> GetReadyKeys() const;
  KeysWithStatus GetSessionKeys(const std::string& session_id) const;

static  std::string hex2string(const uint8_t *data, int len){
      char hex_map[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
      std::string hex;

      for (int i = 0; i < len; i++) {
          int lower = data[i] & 0x0f;
          int upper = (data[i] >> 4) & 0x0f;
          hex.push_back(hex_map[upper]);
          hex.push_back(hex_map[lower]);
      }
      return hex;
  }

static  const char * keyStatusToString(SbDrmKeyStatus status) {
      const char * str = "unknown";
      switch (status) {
          case kSbDrmKeyStatusUsable:
              str = "Usable";
              break;
          case kSbDrmKeyStatusExpired:
              str = "Expired";
              break;
          case kSbDrmKeyStatusReleased:
              str = "Released";
              break;
          case kSbDrmKeyStatusRestricted:
              str = "Restricted";
              break;
          case kSbDrmKeyStatusDownscaled:
              str = "Downscaled";
              break;
          case kSbDrmKeyStatusPending:
              str = "Pending";
              break;
          case kSbDrmKeyStatusError:
              str = "Error";
              break;
          default:
              str = "unknown status";
              break;
      }

      return str;
  }

 private:
  session::Session* GetSessionById(const std::string& id) const;
  void AnnounceKeys();

  std::set<std::string> GetReadyKeysUnlocked() const;

  std::string key_system_;

  std::string metrics_data_;

  ::starboard::shared::starboard::ThreadChecker thread_checker_;
  void* context_;
  std::vector<std::unique_ptr<session::Session>> sessions_;

  const SbDrmSessionUpdateRequestFunc session_update_request_callback_;
  const SbDrmSessionUpdatedFunc session_updated_callback_;
  const SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback_;
  const SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback_;
  const SbDrmSessionClosedFunc session_closed_callback_;

  OpenCDMSystem* ocdm_system_;
  std::vector<Observer*> observers_;
  std::unordered_map<std::string, KeysWithStatus> session_keys_;
  mutable std::set<std::string> cached_ready_keys_;
  SbEventId event_id_;
  ::starboard::Mutex mutex_;
};

}  // namespace drm
}  // namespace shared
}  // namespace rdk
}  // namespace starboard
}  // namespace third_party

#endif  // THIRD_PARTY_STARBOARD_RDK_SHARED_DRM_DRM_SYSTEM_OCDM_H_
