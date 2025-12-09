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

#include "starboard/common/string.h"
#include "starboard/media.h"
// #include "starboard/shared/starboard/media/media_support_internal.h"
// #include "starboard/shared/widevine/drm_system_widevine.h"
#include "starboard/tvos/shared/media/drm_system_platform.h"

namespace starboard::shared::starboard::media {

// Array of supported key systems for tvOS
static const char* kSupportedKeySystemNames[] = {
    "com.youtube.widevine.l3", "com.youtube.widevine.forcehdcp"};

static constexpr int kSupportedKeySystemNamesCount =
    sizeof(kSupportedKeySystemNames) / sizeof(kSupportedKeySystemNames[0]);

bool MediaIsSupported(SbMediaVideoCodec video_codec,
                      SbMediaAudioCodec audio_codec,
                      const char* key_system) {
  if (strchr(key_system, ';')) {
    // TODO: Remove this check and enable key system with attributes support.
    return false;
  }

  for (int i = 0; i < kSupportedKeySystemNamesCount; i++) {
    if (strcmp(kSupportedKeySystemNames[i], key_system) == 0) {
      return true;
    }
  }

  if (key_system == ::starboard::DrmSystemPlatform::GetKeySystemName()) {
    // We don't use AVPlayer for encrypted vp9.
    return video_codec != kSbMediaVideoCodecVp9;
  }

  // Only encrypted VP9 and AAC are supported.
  return ::starboard::DrmSystemPlatform::IsKeySystemSupported(key_system) &&
         (video_codec == kSbMediaVideoCodecNone ||
          video_codec == kSbMediaVideoCodecVp9) &&
         (audio_codec == kSbMediaAudioCodecNone ||
          audio_codec == kSbMediaAudioCodecAac);
}

// Returns the total count of supported key systems, including Widevine
// systems and platform DRM (e.g., FairPlay).
int GetSupportedKeySystemNamesCount() {
  // Cache platform DRM key system name (e.g., FairPlay on Apple platforms)
  static const std::string platform_key_system =
      ::starboard::DrmSystemPlatform::GetKeySystemName();
  return kSupportedKeySystemNamesCount + (platform_key_system.empty() ? 0 : 1);
}

// Fills the output array with supported key system names. Includes Widevine
// key systems from kSupportedKeySystemNames array and the platform DRM system
// if available.
int GetSupportedKeySystemNames(const char* out_key_system_names[],
                               int capacity) {
  if (!out_key_system_names || capacity <= 0) {
    return 0;
  }

  // Cache platform DRM key system name (e.g., FairPlay on Apple platforms)
  static const std::string platform_key_system =
      ::starboard::DrmSystemPlatform::GetKeySystemName();

  int count = 0;
  // Add Widevine key systems
  for (int i = 0; i < kSupportedKeySystemNamesCount && count < capacity; i++) {
    out_key_system_names[count] = kSupportedKeySystemNames[i];
    count++;
  }

  // Add platform DRM key system if available
  if (!platform_key_system.empty() && count < capacity) {
    out_key_system_names[count] = platform_key_system.c_str();
    count++;
  }

  return count;
}

}  // namespace starboard::shared::starboard::media
