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
#import "starboard/tvos/shared/media/application_drm_system.h"
#import "starboard/tvos/shared/media/drm_manager.h"
#include "starboard/tvos/shared/media/drm_system_platform.h"
#import "starboard/tvos/shared/starboard_application.h"

using starboard::DrmSystemPlatform;
using starboard::DrmSystemWidevine;

void SbDrmUpdateServerCertificate(SbDrmSystem drm_system,
                                  int ticket,
                                  const void* certificate,
                                  int certificate_size) {
  if (!SbDrmSystemIsValid(drm_system)) {
    SB_DLOG(ERROR) << "Invalid DRM system.";
    return;
  }

  if (ticket == kSbDrmTicketInvalid) {
    SB_DLOG(ERROR) << "Ticket must be specified.";
    return;
  }

  // Standard EME: forward certificate to SBDApplicationDrmSystem for FairPlay.
  // This enables the "skd" init data type path where the certificate is
  // provided via setServerCertificate() (like WebKit/Safari) rather than
  // packed into generateRequest() init data (like C25/YouTube).
  // WebKit ref: CDMInstanceFairPlayStreamingAVFObjC.mm:394-409
  @autoreleasepool {
    SBDDrmManager* drmManager = SBDGetApplication().drmManager;
    if ([drmManager isApplicationDrmSystem:drm_system]) {
      SBDApplicationDrmSystem* applicationDrmSystem =
          [drmManager applicationDrmSystemForStarboardDrmSystem:drm_system];
      NSData* certData = [NSData dataWithBytes:certificate
                                        length:certificate_size];
      NSLog(@"[ABHIJEET][FPS-FLOW] SbDrmUpdateServerCertificate:"
            @" forwarding %d bytes to SBDApplicationDrmSystem",
            certificate_size);
      // updateServerCertificate: stores the cert and fires the
      // _serverCertificateUpdatedFunc callback to resolve the JS promise.
      [applicationDrmSystem updateServerCertificate:certData ticket:ticket];
      return;
    }
  }

  SB_DCHECK(DrmSystemWidevine::IsDrmSystemWidevine(drm_system) ||
            DrmSystemPlatform::IsSupported(drm_system));
  drm_system->UpdateServerCertificate(ticket, certificate, certificate_size);
}
