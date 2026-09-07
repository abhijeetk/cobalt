// Copyright 2026 The Cobalt Authors. All Rights Reserved.
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

#ifndef MEDIA_BASE_PLATFORM_INIT_DATA_TYPES_H_
#define MEDIA_BASE_PLATFORM_INIT_DATA_TYPES_H_

#include <string>

#include "media/base/eme_constants.h"
#include "media/base/media_export.h"

namespace media {

// Runtime registry for platform-specific EME init data type mappings.
//
// Platform-specific init data types (e.g. those used by URL player DRM) are
// registered at startup by internal code, keeping the actual string values
// out of public code. Public code only sees the EmeInitDataType enum values.
class MEDIA_EXPORT PlatformInitDataTypes {
 public:
  // Register a platform-specific init data type string and its corresponding
  // enum value. Called once at startup by internal code.
  static void Register(const std::string& type_string,
                       EmeInitDataType enum_value);

  // Look up the enum value for a platform-specific init data type string.
  // Returns EmeInitDataType::UNKNOWN if not registered.
  static EmeInitDataType ToEnum(const std::string& type_string);

  // Look up the string for a platform-specific EmeInitDataType enum value.
  // Returns an empty string if not registered.
  static const std::string& ToString(EmeInitDataType enum_value);
};

}  // namespace media

#endif  // MEDIA_BASE_PLATFORM_INIT_DATA_TYPES_H_
