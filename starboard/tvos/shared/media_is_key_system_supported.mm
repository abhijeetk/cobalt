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

#include <string_view>

#include "base/containers/contains.h"
#include "starboard/common/string.h"
#include "starboard/media.h"
#include "starboard/tvos/shared/media/drm_system_platform.h"

namespace starboard {

bool MediaIsKeySystemSupported(SbMediaVideoCodec video_codec,
                               SbMediaAudioCodec audio_codec,
                               const char* key_system) {
  if (strchr(key_system, ';')) {
    // TODO: Remove this check and enable key system with attributes support.
    return false;
  }

  constexpr std::string_view kSupportedWidevineSystems[] = {
      "com.youtube.widevine.l3",
      "com.youtube.widevine.forcehdcp",
      "com.widevine.alpha",
  };

  if (base::Contains(kSupportedWidevineSystems, key_system)) {
    return true;
  }

  if (key_system == ::starboard::DrmSystemPlatform::GetKeySystemName()) {
    // We don't use AVPlayer for encrypted vp9.
    return video_codec != kSbMediaVideoCodecVp9;
  }

  // AVSBDL FairPlay path: supports H.264, VP9, and AAC.
  // Originally restricted to VP9-only, but HLS FairPlay streams also use H.264.
  bool is_platform_supported =
      ::starboard::DrmSystemPlatform::IsKeySystemSupported(key_system);
  bool is_video_supported = (video_codec == kSbMediaVideoCodecNone ||
                             video_codec == kSbMediaVideoCodecVp9 ||
                             video_codec == kSbMediaVideoCodecH264);
  bool is_audio_supported = (audio_codec == kSbMediaAudioCodecNone ||
                             audio_codec == kSbMediaAudioCodecAac);
  SB_LOG(INFO) << "[ABHIJEET][DRM] MediaIsKeySystemSupported SBDL path:"
               << " key_system=" << key_system << " video_codec=" << video_codec
               << " audio_codec=" << audio_codec
               << " platform=" << is_platform_supported
               << " video=" << is_video_supported
               << " audio=" << is_audio_supported;
  return is_platform_supported && is_video_supported && is_audio_supported;
}

}  // namespace starboard
