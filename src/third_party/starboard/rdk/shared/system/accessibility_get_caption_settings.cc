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

#include "base/compiler_specific.h"

#include "starboard/accessibility.h"
#include "starboard/common/memory.h"
#include <iostream>
#include <string>
#include <sys/types.h>
#include <unistd.h>
using namespace std;
#if SB_API_VERSION >= 12 || SB_HAS(CAPTIONS)
bool SbAccessibilityGetCaptionSettings(
    SbAccessibilityCaptionSettings* caption_settings) {



  if (!caption_settings ||
      !starboard::common::MemoryIsZero(
        caption_settings, sizeof(SbAccessibilityCaptionSettings))) {
    return false;
  }
	char* text = getenv("CAPTIONS");
  string STR_TRUE("TRUE");
  string  STR_true("true");
	if (text != nullptr) {
    string value(text);
		if ((value == STR_TRUE)||(value == STR_true))
		  caption_settings->is_enabled = true;
	}

	text = getenv("CAPTIONS_SUPPORT");
	if (text != nullptr) {
    string value(text);
		if ((value == STR_TRUE)||(value == STR_true))
			caption_settings->supports_is_enabled = true;
	}


  
  // Since kSbAccessibilityCaptionStateUnsupported == 0, there is no need to
  // explicitly set states to kSbAccessibilityCaptionStateUnsupported.
  return true;
}
#endif  // SB_API_VERSION >= 12 || SB_HAS(CAPTIONS)
