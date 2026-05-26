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

#import "starboard/tvos/shared/media/application_drm_system.h"

#import <AVFoundation/AVFoundation.h>

#import "starboard/tvos/shared/defines.h"
#import "starboard/tvos/shared/media/drm_manager.h"
#import "starboard/tvos/shared/starboard_application.h"

static NSString* SBDHexPrefix(NSData* data, NSUInteger maxBytes) {
  if (!data) {
    return @"<nil>";
  }
  const uint8_t* bytes = static_cast<const uint8_t*>(data.bytes);
  NSUInteger length = MIN(data.length, maxBytes);
  NSMutableString* result = [NSMutableString stringWithCapacity:length * 2];
  for (NSUInteger i = 0; i < length; ++i) {
    [result appendFormat:@"%02x", bytes[i]];
  }
  if (data.length > maxBytes) {
    [result appendString:@"..."];
  }
  return result;
}

@interface SBDKeyPrefetchData : NSObject
@property(readonly) NSData* certificationData;
@property(readonly) NSData* contentIdentifier;
@property(readonly) NSData* initializationData;
@property(readonly) NSInteger ticket;
@end

@implementation SBDKeyPrefetchData
@synthesize certificationData = _certificationData;
@synthesize contentIdentifier = _contentIdentifier;
@synthesize initializationData = _initializationData;
@synthesize ticket = _ticket;

- (instancetype)initWithCertificationData:(NSData*)certificationData
                        contentIdentifier:(NSData*)contentIdentifier
                                 initData:(NSData*)initData
                                   ticket:(NSInteger)ticket {
  self = [super init];
  if (self) {
    _certificationData = certificationData;
    _contentIdentifier = contentIdentifier;
    _initializationData = initData;
    _ticket = ticket;
  }
  return self;
}
@end

@implementation SBDApplicationDrmSystem {
  /**
   *  @brief Context parameter to be passed to session callback functions.
   */
  void* _sessionContext;

  /**
   *  @brief Callback that will receive generated session update request when
   *      requested from the DRM system.
   */
  SbDrmSessionUpdateRequestFunc _sessionUpdateRequestFunc;

  /**
   *  @brief A callback for notifications that a session has been added, and
   *      subsequent encrypted samples are actively ready to be decoded.
   */
  SbDrmSessionUpdatedFunc _sessionUpdatedFunc;

  /**
   *  @brief A callback for notifications that the server certificate has
   *      been updated. Used by standard EME setServerCertificate() flow
   *      ("skd" path). Not used by C25/YouTube "fairplay" path.
   */
  SbDrmServerCertificateUpdatedFunc _serverCertificateUpdatedFunc;

  /**
   *  @brief Data for key prefetches.
   */
  NSMutableDictionary<NSString*, SBDKeyPrefetchData*>* _keyPrefetchData;

  /**
   *  @brief The @c AVContentKeyRequests that are waiting to receive a
   *      generateSessionUpdateRequest.
   */
  NSMutableDictionary<NSString*, AVContentKeyRequest*>*
      _keyRequestsPendingUpdateRequest;

  /**
   *  @brief The @c AVContentKeyRequests that are waiting to receive a
   *      key.
   */
  NSMutableDictionary<NSData*, AVContentKeyRequest*>* _keyRequestsPendingKey;
}

- (instancetype)initWithSessionContext:(void*)context
              sessionUpdateRequestFunc:
                  (SbDrmSessionUpdateRequestFunc)updateRequestFunc
                    sessionUpdatedFunc:(SbDrmSessionUpdatedFunc)updatedFunc
          serverCertificateUpdatedFunc:
              (SbDrmServerCertificateUpdatedFunc)serverCertificateUpdatedFunc {
  self = [super init];
  if (self) {
    _sessionContext = context;
    _sessionUpdateRequestFunc = updateRequestFunc;
    _sessionUpdatedFunc = updatedFunc;
    _serverCertificateUpdatedFunc = serverCertificateUpdatedFunc;
    _keyRequestsPendingUpdateRequest = [NSMutableDictionary dictionary];
    _keyRequestsPendingKey = [NSMutableDictionary dictionary];
    _keyPrefetchData = [NSMutableDictionary dictionary];
  }
  return self;
}

- (void)updateServerCertificate:(NSData*)certificate ticket:(NSInteger)ticket {
  // Standard EME: store certificate for later use in SPC generation.
  // This differs from C25/YouTube where the cert was packed inside
  // generateRequest() init data. In standard EME ("skd" path), the cert
  // is provided separately via setServerCertificate().
  // WebKit ref: CDMInstanceFairPlayStreamingAVFObjC.mm:394-409
  //   m_serverCertificate = WTF::move(serverCertificate);
  @synchronized(self) {
    _serverCertificate = certificate;
  }
  NSLog(@"[ABHIJEET][FPS-FLOW] updateServerCertificate: stored %lu bytes",
        (unsigned long)certificate.length);

  // Fire callback to resolve the JS setServerCertificate() promise.
  // Pattern matches how _sessionUpdateRequestFunc and _sessionUpdatedFunc
  // fire their callbacks to resolve promises in StarboardCdm.
  if (_serverCertificateUpdatedFunc) {
    SBDDrmManager* drmManager = SBDGetApplication().drmManager;
    SbDrmSystem starboardDrmSystem =
        [drmManager starboardDrmSystemForApplicationDrmSystem:self];
    NSLog(@"[ABHIJEET][FPS-FLOW] updateServerCertificate: firing callback"
          @" ticket=%ld",
          (long)ticket);
    _serverCertificateUpdatedFunc(starboardDrmSystem, _sessionContext,
                                  (int)ticket, kSbDrmStatusSuccess, "");
  }
}

- (void)generateSessionUpdateRequestForSkd:(NSData*)initData
                                    ticket:(NSInteger)ticket {
  // Standard EME "skd" path, matching WebKit behavior:
  // CDMInstanceFairPlayStreamingAVFObjC.mm:832-833:
  //   identifier = [[NSString alloc] initWithData:...
  //   encoding:NSUTF8StringEncoding];
  // CDMInstanceFairPlayStreamingAVFObjC.mm:1240-1255:
  //   appIdentifier = m_instance->serverCertificate();
  //   [request makeStreamingContentKeyRequestDataForApp:appIdentifier ...]

  // Step 1: Decode UTF-8 init data to get skd:// identifier
  // (Gap #3 fix: use NSUTF8StringEncoding, not
  // NSUTF16LittleEndianStringEncoding)
  NSString* requestIdentifier =
      [[NSString alloc] initWithData:initData encoding:NSUTF8StringEncoding];
  NSLog(@"[ABHIJEET][FPS-FLOW] generateSessionUpdateRequestForSkd:"
        @" identifier='%@' ticket=%ld",
        requestIdentifier ?: @"(nil - decode failed)", (long)ticket);

  if (!requestIdentifier) {
    NSLog(@"[ABHIJEET][FPS-FLOW]   ERROR: failed to decode init data as UTF-8");
    return;
  }

  // Step 2: Look up the pending AVContentKeyRequest by identifier
  // The key request was queued in _keyRequestsPendingUpdateRequest when
  // processKeyRequest: was called from application_player.mm
  AVContentKeyRequest* keyRequest;
  @synchronized(self) {
    keyRequest = _keyRequestsPendingUpdateRequest[requestIdentifier];
    [_keyRequestsPendingUpdateRequest removeObjectForKey:requestIdentifier];
  }

  NSLog(@"[ABHIJEET][FPS-FLOW]   pending key request lookup: %@,"
        @" pendingCount=%lu",
        keyRequest ? @"FOUND" : @"NOT FOUND",
        (unsigned long)_keyRequestsPendingUpdateRequest.count);

  if (!keyRequest) {
    NSLog(@"[ABHIJEET][FPS-FLOW]   No pending key request for identifier '%@'."
          @" Manually triggering AVContentKeySession (AVSBDL path).",
          requestIdentifier);

    // AVSBDL path: no AVPlayer, so AVContentKeySession was never triggered
    // automatically. Create the session if needed and manually request a key,
    // following the same pattern as the "fairplay" prefetch path.

    // Ensure AVContentKeySession exists
    if (!self.keySession) {
      NSLog(@"[ABHIJEET][FPS-FLOW]   Creating AVContentKeySession for AVSBDL");
      self.keySession = [AVContentKeySession
          contentKeySessionWithKeySystem:AVContentKeySystemFairPlayStreaming];
      [self.keySession setDelegate:self
                             queue:dispatch_queue_create(
                                       "com.cobalt.fairplay.keysession", NULL)];
    }

    // Get server certificate
    NSData* certData;
    @synchronized(self) {
      certData = _serverCertificate;
    }
    if (!certData) {
      NSLog(@"[ABHIJEET][FPS-FLOW]   ERROR: no server certificate for AVSBDL"
            @" key request. Was setServerCertificate() called?");
      return;
    }

    // Store prefetch data so processKeyRequest: can fulfill it when the
    // delegate fires contentKeySession:didProvideContentKeyRequest:
    NSData* contentIdentifier =
        [requestIdentifier dataUsingEncoding:NSUTF8StringEncoding];
    SBDKeyPrefetchData* prefetchData =
        [[SBDKeyPrefetchData alloc] initWithCertificationData:certData
                                            contentIdentifier:contentIdentifier
                                                     initData:initData
                                                       ticket:ticket];
    @synchronized(self) {
      _keyPrefetchData[requestIdentifier] = prefetchData;
    }

    NSLog(@"[ABHIJEET][FPS-FLOW]   Calling processContentKeyRequestWith"
          @"Identifier: '%@'",
          requestIdentifier);
    [self.keySession processContentKeyRequestWithIdentifier:requestIdentifier
                                         initializationData:nil
                                                    options:nil];
    return;
  }

  // Step 3: Get the stored server certificate (from setServerCertificate)
  NSData* certData;
  @synchronized(self) {
    certData = _serverCertificate;
  }

  NSLog(@"[ABHIJEET][FPS-FLOW]   serverCertificate: %@, size=%lu",
        certData ? @"present" : @"nil",
        (unsigned long)(certData ? certData.length : 0));

  if (!certData) {
    NSLog(@"[ABHIJEET][FPS-FLOW]   ERROR: no server certificate stored."
          @" Was setServerCertificate() called before generateRequest()?");
    return;
  }

  // Step 4: Extract content identifier from the skd:// URI
  // The content identifier is typically the part after "skd://"
  // WebKit ref: CDMInstanceFairPlayStreamingAVFObjC.mm:1252
  //   contentIdentifier = keyIDs.first()->makeContiguous()->createNSData();
  NSData* contentIdentifier =
      [requestIdentifier dataUsingEncoding:NSUTF8StringEncoding];

  NSLog(
      @"[ABHIJEET][FPS-FLOW]   calling makeStreamingContentKeyRequestDataForApp"
      @" certSize=%lu contentIdSize=%lu",
      (unsigned long)certData.length, (unsigned long)contentIdentifier.length);

  // Step 5: Generate SPC using Apple's API
  // WebKit ref: CDMInstanceFairPlayStreamingAVFObjC.mm:1255
  //   [request makeStreamingContentKeyRequestDataForApp:appIdentifier
  //            contentIdentifier:contentIdentifier ...]
  [self makeKeyRequestData:keyRequest
         certificationData:certData
         contentIdentifier:contentIdentifier
                  initData:initData
                    ticket:ticket];
}

- (void)updateSessionWithKey:(NSData*)key
                      ticket:(NSInteger)ticket
                   sessionId:(NSData*)sessionId {
  NSLog(@"[ABHIJEET][FPS-FLOW] updateSessionWithKey: keySize=%lu ticket=%ld"
        @" sessionIdSize=%lu keyHex=%@",
        (unsigned long)key.length, (long)ticket,
        (unsigned long)sessionId.length, SBDHexPrefix(key, 64));

  AVContentKeyRequest* keyRequest;
  @synchronized(self) {
    keyRequest = _keyRequestsPendingKey[sessionId];
    [_keyRequestsPendingKey removeObjectForKey:sessionId];
  }

  NSLog(@"[ABHIJEET][FPS-FLOW]   keyRequest=%@ identifier='%@'",
        keyRequest ? @"FOUND" : @"NOT FOUND",
        keyRequest ? keyRequest.identifier : @"n/a");

  AVContentKeyResponse* keyResponse = [AVContentKeyResponse
      contentKeyResponseWithFairPlayStreamingKeyResponseData:key];
  NSLog(@"[ABHIJEET][FPS-FLOW]   Calling processContentKeyResponse"
        @" (CKC -> AVContentKeyRequest)");
  [keyRequest processContentKeyResponse:keyResponse];
  NSLog(@"[ABHIJEET][FPS-FLOW]   processContentKeyResponse returned,"
        @" contentKey=%@",
        keyRequest.contentKey ? @"present" : @"nil");

  SBDDrmManager* drmManager = SBDGetApplication().drmManager;
  SbDrmSystem starboardDrmSystem =
      [drmManager starboardDrmSystemForApplicationDrmSystem:self];
  const char* errorMessage = NULL;
  _sessionUpdatedFunc(starboardDrmSystem, _sessionContext, ticket,
                      kSbDrmStatusSuccess, errorMessage, sessionId.bytes,
                      sessionId.length);
}

- (void)makeKeyRequestData:(AVContentKeyRequest*)keyRequest
         certificationData:(NSData*)certificationData
         contentIdentifier:(NSData*)contentIdentifier
                  initData:(NSData*)initData
                    ticket:(NSInteger)ticket {
  NSLog(@"[ABHIJEET][FPS-FLOW] makeKeyRequestData:"
        @" identifier='%@' certLen=%lu contentIdentifierLen=%lu"
        @" contentIdentifierHex=%@ initDataLen=%lu ticket=%ld",
        keyRequest.identifier, (unsigned long)certificationData.length,
        (unsigned long)contentIdentifier.length,
        SBDHexPrefix(contentIdentifier, 80), (unsigned long)initData.length,
        (long)ticket);
  [keyRequest
      makeStreamingContentKeyRequestDataForApp:certificationData
                             contentIdentifier:contentIdentifier
                                       options:nil
                             completionHandler:^(
                                 NSData* _Nullable contentKeyRequestData,
                                 NSError* _Nullable error) {
                               if (error) {
                                 [keyRequest
                                     processContentKeyResponseError:error];
                                 return;
                               }
                               NSLog(
                                   @"[ABHIJEET][FPS-FLOW] makeKeyRequestData"
                                   @" completed: spcLen=%lu spcHex=%@",
                                   (unsigned long)contentKeyRequestData.length,
                                   SBDHexPrefix(contentKeyRequestData, 64));
                               [self streamingContentKeyRequest:keyRequest
                                              completedWithData:
                                                  contentKeyRequestData
                                                       initData:initData
                                                         ticket:ticket];
                             }];
}

- (void)
    generateSessionUpdateRequestWithCertificationData:(NSData*)certificationData
                                    contentIdentifier:(NSData*)contentIdentifier
                                             initData:(NSData*)initData
                                               ticket:(NSInteger)ticket {
  NSString* requestIdentifier =
      [[NSString alloc] initWithData:initData
                            encoding:NSUTF16LittleEndianStringEncoding];

  // This flow is also used to initiate prefetching of a content key. If no
  // key request can be found for the given init data, then a prefetch was
  // requested.
  AVContentKeyRequest* keyRequest;
  @synchronized(self) {
    keyRequest = _keyRequestsPendingUpdateRequest[requestIdentifier];
    [_keyRequestsPendingUpdateRequest removeObjectForKey:requestIdentifier];
  }

  if (!keyRequest) {
    // This is a key prefetch. Cache the data for use once the keyRequest
    // is generated.
    SBDKeyPrefetchData* prefetchData =
        [[SBDKeyPrefetchData alloc] initWithCertificationData:certificationData
                                            contentIdentifier:contentIdentifier
                                                     initData:initData
                                                       ticket:ticket];
    @synchronized(self) {
      _keyPrefetchData[requestIdentifier] = prefetchData;
    }

    // Tell the key session to generate a key request with the given
    // identifier. This will go through the system just as if AVPlayer
    // initiated a content key request.
    [_keySession processContentKeyRequestWithIdentifier:requestIdentifier
                                     initializationData:nil
                                                options:nil];
    return;
  }

  [self makeKeyRequestData:keyRequest
         certificationData:certificationData
         contentIdentifier:contentIdentifier
                  initData:initData
                    ticket:ticket];
}

- (void)streamingContentKeyRequest:(AVContentKeyRequest*)keyRequest
                 completedWithData:(NSData*)contentKeyRequestData
                          initData:(NSData*)initData
                            ticket:(NSInteger)ticket {
  NSData* sessionId = [NSData dataWithBytes:&ticket length:sizeof(ticket)];
  @synchronized(self) {
    _keyRequestsPendingKey[sessionId] = keyRequest;
  }

  SBDDrmManager* drmManager = SBDGetApplication().drmManager;
  SbDrmSystem starboardDrmSystem =
      [drmManager starboardDrmSystemForApplicationDrmSystem:self];
  const char* errorMessage = NULL;
  _sessionUpdateRequestFunc(
      starboardDrmSystem, _sessionContext, ticket, kSbDrmStatusSuccess,
      kSbDrmSessionRequestTypeLicenseRequest, errorMessage, sessionId.bytes,
      sessionId.length, contentKeyRequestData.bytes,
      contentKeyRequestData.length, static_cast<const char*>(initData.bytes));
}

- (BOOL)processKeyRequest:(AVContentKeyRequest*)keyRequest {
  @synchronized(self) {
    // Check if this keyRequest matches a prefetch.
    SBDKeyPrefetchData* prefetchData = _keyPrefetchData[keyRequest.identifier];
    if (prefetchData) {
      // Fulfill the prefetch.
      [_keyPrefetchData removeObjectForKey:keyRequest.identifier];
      [self makeKeyRequestData:keyRequest
             certificationData:prefetchData.certificationData
             contentIdentifier:prefetchData.contentIdentifier
                      initData:prefetchData.initializationData
                        ticket:prefetchData.ticket];
      return NO;
    }

    // No information is available for this key request, so inform the web app
    // that encrypted media was encountered.
    _keyRequestsPendingUpdateRequest[keyRequest.identifier] = keyRequest;
    return YES;
  }
}

#pragma mark - AVContentKeySessionDelegate (AVSBDL path)

- (void)contentKeySession:(AVContentKeySession*)session
    didProvideContentKeyRequest:(AVContentKeyRequest*)keyRequest {
  NSLog(@"[ABHIJEET][FPS-FLOW] SBDApplicationDrmSystem"
        @" didProvideContentKeyRequest: identifier='%@'",
        keyRequest.identifier);
  [self processKeyRequest:keyRequest];
}

- (nullable AVContentKey*)contentKeyForIdentifier:(const uint8_t*)key_id
                                      key_id_size:(int)key_id_size {
  // Not used for the SBDApplicationDrmSystem path (URL player).
  // DrmSystemFairplay has its own content key storage.
  NSLog(@"[ABHIJEET][FPS-FLOW] SBDApplicationDrmSystem contentKeyForIdentifier"
        @" (not implemented for URL player path)");
  return nil;
}

- (void)contentKeySession:(AVContentKeySession*)session
    didProvideRenewingContentKeyRequest:(AVContentKeyRequest*)keyRequest {
  NSLog(@"[ABHIJEET][FPS-FLOW] SBDApplicationDrmSystem"
        @" didProvideRenewingContentKeyRequest: identifier='%@'",
        keyRequest.identifier);
  [self processKeyRequest:keyRequest];
}

- (void)contentKeySession:(AVContentKeySession*)session
        contentKeyRequest:(AVContentKeyRequest*)keyRequest
         didFailWithError:(NSError*)err {
  NSLog(@"[ABHIJEET][FPS-FLOW] SBDApplicationDrmSystem"
        @" contentKeyRequest didFailWithError: %@",
        err);
}

@end
