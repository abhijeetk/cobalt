// Copyright 2019 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/common/log.h"
#include "starboard/drm.h"
#include "starboard/shared/starboard/drm/drm_system_internal.h"
#include "starboard/shared/widevine/drm_system_widevine.h"
#import "starboard/tvos/shared/media/drm_manager.h"
#include "starboard/tvos/shared/media/drm_system_platform.h"
#import "starboard/tvos/shared/starboard_application.h"

using starboard::DrmSystemPlatform;
using starboard::DrmSystemWidevine;

bool SbDrmIsServerCertificateUpdatable(SbDrmSystem drm_system) {
  if (!SbDrmSystemIsValid(drm_system)) {
    SB_DLOG(ERROR) << "Invalid DRM system.";
    return false;
  }

  @autoreleasepool {
    SBDDrmManager* drmManager = SBDGetApplication().drmManager;
    if ([drmManager isApplicationDrmSystem:drm_system]) {
      // Standard EME: setServerCertificate() is supported for FairPlay.
      // This differs from C25 which returned false here because YouTube
      // packs the certificate into generateRequest() init data instead.
      // We now support both paths:
      //   "skd" type:     cert via setServerCertificate() (standard EME)
      //   "fairplay" type: cert packed in generateRequest() (YouTube/C25)
      NSLog(@"[ABHIJEET][FPS-FLOW] IsServerCertificateUpdatable:"
            @" returning true for application DRM (FairPlay)");
      return true;
    }
  }

  SB_DCHECK(DrmSystemWidevine::IsDrmSystemWidevine(drm_system) ||
            DrmSystemPlatform::IsSupported(drm_system));
  return drm_system->IsServerCertificateUpdatable();
}
