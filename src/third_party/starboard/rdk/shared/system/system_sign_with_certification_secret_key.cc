
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
#include <memory>
#include <vector>

#include "starboard/system.h"
#include "starboard/string.h"
#include "starboard/memory.h"

#if 1
#if SB_API_VERSION >= 11
#include "youtube_api.h"
#include "starboard/common/log.h"
bool SbSystemSignWithCertificationSecretKey(const uint8_t* message,
                                            size_t message_size_in_bytes,
                                            uint8_t* digest,
                                            size_t digest_size_in_bytes) {
  int ret;
  SB_LOG(INFO) << "Start SbSystemSignWithCertificationSecretKey";
  if (yt_init())
  {
    SB_LOG(WARNING) << " yt_init failed";
    return false;
  }
  ret = yt_hmac_sha256(message, message_size_in_bytes , digest, &digest_size_in_bytes);
  yt_exit();
  if (ret)
  {
    SB_LOG(WARNING) << " SbSystemSignWithCertificationSecretKey failed";
  }
  return (ret==0);
}
#endif
#else
#include "third_party/starboard/rdk/shared/hang_detector.h"
#include "third_party/starboard/rdk/shared/log_override.h"

#if defined(HAS_CRYPTOGRAPHY)
#include <cryptography/cryptography.h>
#include <rfcapi.h>
#endif

namespace {

template<typename T>
struct RefDeleter {
  void operator()(T* ref) { if ( ref ) ref->Release(); }
};

template<typename T>
using ScopedRef = std::unique_ptr<T, RefDeleter<T>>;

}

bool SbSystemSignWithCertificationSecretKey(const uint8_t* message,
                                            size_t message_size_in_bytes,
                                            uint8_t* digest,
                                            size_t digest_size_in_bytes) {
  bool result = false;

#if defined(HAS_CRYPTOGRAPHY)
  using namespace WPEFramework::Cryptography;

  const char kDefaultKeyName[] = "0381000003810001.key";
  const char kRFCParamName[] = "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.Cobalt.AuthCertKeyName";

  third_party::starboard::rdk::shared::HangMonitor hang_monitor(__func__);

  std::string key_name;
  const char *env = std::getenv("COBALT_CERT_KEY_NAME");
  if ( env != nullptr ) {
    key_name = env;
    SB_LOG(INFO) << "Using ENV set key name: '" << key_name << "'";
  } else {
    char *callerId = SbStringDuplicate("Cobalt");
    RFC_ParamData_t param;
    SbMemorySet(&param, 0, sizeof (param));
    WDMP_STATUS status = getRFCParameter(callerId, kRFCParamName, &param);
    if ( status == WDMP_SUCCESS && param.type == WDMP_STRING ) {
      key_name = param.value;
      SB_LOG(INFO) << "Using RFC provided key name: '" << key_name << "'";
    }
    SbMemoryDeallocate(callerId);
  }

  if ( key_name.empty() ) {
    key_name = kDefaultKeyName;
    SB_LOG(INFO) << "Using default key name: '" << key_name << "'";
  }

  ScopedRef<ICryptography> icrypto;
  ScopedRef<IVault> vault;
  ScopedRef<IHash> hash;

  icrypto.reset( ICryptography::Instance(EMPTY_STRING) );
  if ( !icrypto ) {
    SB_LOG(ERROR) << "Failed to create ICryptography instance";
    return false;
  }

  vault.reset( icrypto->Vault(cryptographyvault::CRYPTOGRAPHY_VAULT_DEFAULT) );
  if ( !vault ) {
    SB_LOG(ERROR) << "Failed to get default vault";
    return false;
  }

  uint32_t key_id, rc;
  if ( (key_id = vault->Import(key_name.size(), reinterpret_cast<const uint8_t*>(key_name.c_str()), true)) == 0 ) {
    SB_LOG(ERROR) << "Failed to import key";
    return false;
  } else {
    hash.reset( vault->HMAC(hashtype::SHA256, key_id) );
  }

  if ( !hash ) {
    SB_LOG(ERROR) << "Vault returned null HMAC for key id: 0x" << std::hex << key_id;
  }
  else if ( (rc = hash->Ingest( message_size_in_bytes, message )) != message_size_in_bytes ) {
    SB_LOG(ERROR) << "HMAC 'Ingest' failed, rc: " << rc << " message size: " << message_size_in_bytes;
  }
  else if ( (rc = hash->Calculate( digest_size_in_bytes, digest )) != digest_size_in_bytes ) {
    SB_LOG(ERROR) << "HMAC 'Calculate' failed, rc: " << rc << " digest size: " << digest_size_in_bytes;
  }
  else {
    result = true;
  }
#endif

  return result;
}
#endif
