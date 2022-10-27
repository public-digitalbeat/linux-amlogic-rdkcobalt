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
#include <cryptography/cryptography.h>
#include <core/core.h>

using namespace WPEFramework::Cryptography;

int main()
{
  ICryptography *cg;
  IHash   *hashImpl;
  IVault  *vault;
  uint32_t keyId;
  uint8_t  output [128] = { 0 };
  uint8_t  data [128] = { 0 };
  const uint8_t blob[] = "xxxxxxxxxxxxxxxxxxxx";

  cg = ICryptography::Instance("");
  vault = cg->Vault(cryptographyvault::CRYPTOGRAPHY_VAULT_DEFAULT);
  keyId = vault->Import(sizeof(blob), blob, true);
  hashImpl = vault->HMAC(hashtype::SHA256, keyId);
  hashImpl->Ingest(sizeof(data), data);
  hashImpl->Calculate(sizeof(output), output);
  hashImpl->Release();
  vault->Release();
  cg->Release();

  return 0;
}
