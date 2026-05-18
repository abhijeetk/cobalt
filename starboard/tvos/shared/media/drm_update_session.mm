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

#include "starboard/drm.h"
#include "starboard/shared/starboard/drm/drm_system_internal.h"
#include "starboard/shared/widevine/drm_system_widevine.h"
#import "starboard/tvos/shared/media/application_drm_system.h"
#import "starboard/tvos/shared/media/drm_manager.h"
#include "starboard/tvos/shared/media/drm_system_platform.h"
#import "starboard/tvos/shared/starboard_application.h"

using starboard::DrmSystemPlatform;
using starboard::DrmSystemWidevine;

void SbDrmUpdateSession(SbDrmSystem drm_system,
                        int ticket,
                        const void* key,
                        int key_size,
                        const void* session_id,
                        int session_id_size) {
  if (!SbDrmSystemIsValid(drm_system)) {
    SB_DLOG(WARNING) << "Invalid drm system";
    return;
  }

  @autoreleasepool {
    SBDDrmManager* drmManager = SBDGetApplication().drmManager;
    if ([drmManager isApplicationDrmSystem:drm_system]) {
      SBDApplicationDrmSystem* applicationDrmSystem =
          [drmManager applicationDrmSystemForStarboardDrmSystem:drm_system];

      // Try raw binary first (standard EME / "skd" path).
      // Standard FairPlay license servers return raw binary CKC data.
      // WebKit passes it directly to AVContentKeyResponse:
      //   CDMInstanceFairPlayStreamingAVFObjC.mm:1055:
      //   [request processContentKeyResponse:
      //       [AVContentKeyResponse contentKeyResponseWith
      //           FairPlayStreamingKeyResponseData:responseData->createNSData()]]
      //
      // C25/YouTube path sends base64-encoded CKC. Try base64 decode as
      // fallback for backward compatibility.
      NSData* keyData = [NSData dataWithBytes:key length:key_size];

      // Check if the data is base64-encoded (C25/YouTube sends base64)
      NSString* possibleBase64 =
          [[NSString alloc] initWithBytes:(char*)key
                                   length:key_size
                                 encoding:NSUTF8StringEncoding];
      if (possibleBase64) {
        NSData* decoded =
            [[NSData alloc] initWithBase64EncodedString:possibleBase64
                                                options:0];
        if (decoded) {
          // Valid base64: use decoded data (C25/YouTube path)
          NSLog(@"[ABHIJEET][FPS-FLOW] SbDrmUpdateSession: key is base64"
                @" encoded=%d decoded=%lu bytes",
                key_size, (unsigned long)decoded.length);
          keyData = decoded;
        } else {
          // Not valid base64: use raw binary (standard EME path)
          NSLog(@"[ABHIJEET][FPS-FLOW] SbDrmUpdateSession: key is raw"
                @" binary, size=%d bytes",
                key_size);
        }
      } else {
        // Not valid UTF-8: definitely raw binary
        NSLog(@"[ABHIJEET][FPS-FLOW] SbDrmUpdateSession: key is raw"
              @" binary (not UTF-8), size=%d bytes",
              key_size);
      }

      NSData* sessionId = [NSData dataWithBytes:session_id
                                         length:session_id_size];
      NSLog(@"[ABHIJEET][FPS-FLOW] SbDrmUpdateSession: ticket=%d"
            @" keySize=%lu sessionIdSize=%d",
            ticket, (unsigned long)keyData.length, session_id_size);

      [applicationDrmSystem updateSessionWithKey:keyData
                                          ticket:ticket
                                       sessionId:sessionId];
      return;
    }
  }

  SB_DCHECK(DrmSystemWidevine::IsDrmSystemWidevine(drm_system) ||
            DrmSystemPlatform::IsSupported(drm_system));
  drm_system->UpdateSession(ticket, key, key_size, session_id, session_id_size);
}
