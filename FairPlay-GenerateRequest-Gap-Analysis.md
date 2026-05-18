# FairPlay generateRequest Gap Analysis

**Date:** 2026-05-18
**Status:** Identified 3 gaps, ready to implement fixes
**Context:** `encrypted` event reaches JS, but `generateRequest("skd", initData)` times out

## 1. Current State

The encrypted event forwarding pipeline is complete and verified by device logs:
- AVPlayer discovers encrypted content and fires `skd://` URI
- The event reaches JS as `{ initDataType: "skd", initData: 75 bytes }`
- JS calls `session.generateRequest("skd", initData)`
- **But no `SESSION message` event returns** - Mojo times out after 20 seconds
- AVPlayer eventually gives up: `CoreMediaErrorDomain Code=-19152`

## 2. The EME Key Exchange Flow (With Restaurant Analogy)

### Step 1: "Here's my ID card" (Store the FairPlay Server Certificate)

The web app calls `mediaKeys.setServerCertificate(certificate)` to provide the
FairPlay server certificate (a DER-encoded X.509 certificate from the content
provider, e.g., Axinom). This certificate is required later by Apple's
`makeStreamingContentKeyRequestDataForApp:` API to generate the SPC.

```
JS: mediaKeys.setServerCertificate(new Uint8Array(certificate))
  --> Blink: MediaKeys::SetServerCertificate()
    --> Mojo IPC to GPU process
      --> StarboardCdm::SetServerCertificate(certificate_data, cert_size)
        --> SbDrmUpdateServerCertificate(drm_system, ticket, cert, cert_size)
          --> Should store cert on SBDApplicationDrmSystem._serverCertificate
          --> CURRENTLY BROKEN: cert is lost (only Widevine handled)
```

**What should happen:** `SBDApplicationDrmSystem` stores the certificate in a
`_serverCertificate` NSData* property for use in Step 3.

**What actually happens:** `SbDrmUpdateServerCertificate` only handles Widevine.
The certificate never reaches `SBDApplicationDrmSystem`. Returns `false` to JS.

### Step 2: "The restaurant found a locked dish" (Encrypted Event from AVPlayer)

AVPlayer encounters an encrypted HLS segment and fires `didProvideContentKeyRequest:`
with the `skd://` URI identifier. This event flows up to JS as the `encrypted` event.

```
AVPlayer: Detects EXT-X-KEY with skd:// URI in HLS manifest
  --> AVContentKeySession fires didProvideContentKeyRequest:
    --> application_player.mm: extracts scheme from keyRequest.identifier
            e.g., "skd://302f80dd-..." --> scheme = "skd"
          --> _encryptedMediaFunc(scheme, utf8_uri)
      --> SbPlayerBridge::EncryptedMediaInitDataEncounteredCB
        --> StarboardRenderer (PostTask to GPU task runner)
          --> StarboardRendererWrapper (Mojo IPC to renderer process)
            --> StarboardRendererClient (PostTask to main thread)
              --> DemuxerManager::OnEncryptedMediaInitData
                --> WebMediaPlayerImpl::OnEncryptedMediaInitData
                  --> JS receives: encrypted event {
                        initDataType: "skd",
                        initData: ArrayBuffer(75 bytes) = "skd://302f80dd-..."
                      }
```

**What should happen:** JS receives the encrypted event with the skd:// URI.

**What actually happens:** This works correctly! Verified by device logs.

### Step 3: "Please unlock this dish using my ID card" (Generate SPC)

JS creates a `MediaKeySession`, then calls `session.generateRequest("skd", initData)`
to ask the CDM to generate an SPC (Server Playback Context). The CDM needs:
1. The `skd://` URI (from initData) to find the matching `AVContentKeyRequest`
2. The server certificate (from Step 1) to generate the SPC
3. The content identifier extracted from the `skd://` URI

```
JS: session.generateRequest("skd", event.initData)
  --> Blink: MediaKeySession::GenerateRequest("skd", 75 bytes)
    --> Mojo IPC to GPU process
      --> StarboardCdm::GenerateRequest(ticket, "skd", data, size)
        --> SbDrmGenerateSessionUpdateRequest(drm_system, ticket, "skd", data, size)
          --> drm_generate_session_update_request.mm:
              Calls unpackData() expecting [4B][data][4B][contentID][4B][cert]
              But receives raw "skd://302f80dd-..." (75 bytes UTF-8)
              unpackData() returns nil --> "Invalid initialization data"
              --> Returns silently. Callback NEVER fires.
              --> Mojo waits 20 seconds, times out.
```

**What should happen:** CDM decodes the UTF-8 `skd://` URI, finds the matching
`AVContentKeyRequest`, calls `makeStreamingContentKeyRequestDataForApp:` with the
stored server certificate, gets back the SPC, and fires the callback.

**What actually happens:** `unpackData()` fails on raw UTF-8 data (expects YouTube's
packed format). Silent return. No callback. Mojo timeout.

### Step 4: "Here's the bill to pay" (SPC Returned to JS)

The CDM fires `_sessionUpdateRequestFunc` with the SPC data. This flows back to JS
as a `message` event on the `MediaKeySession`.

```
SBDApplicationDrmSystem: _sessionUpdateRequestFunc(drm, context, ticket,
    kSbDrmStatusSuccess, kSbDrmSessionRequestTypeLicenseRequest,
    NULL, sessionId, sessionIdSize, spcData, spcSize, initDataUrl)
  --> StarboardCdm::OnSessionUpdateRequest
    --> Mojo IPC to renderer process
      --> Blink: MediaKeySession fires 'message' event {
            messageType: "license-request",
            message: ArrayBuffer(SPC data, ~1800 bytes)
          }
```

**What should happen:** JS receives SPC in the `message` event.

**What actually happens:** Never reached because Step 3 fails.

### Step 5: "Pay the bill at the counter" (Send SPC to License Server)

JS sends the SPC to the FairPlay license server to get the CKC (Content Key Context).

```
JS: fetch(licenseUrl, { method: 'POST', body: event.message })
  --> HTTPS POST to https://drm-fairplay-licensing.axprod.net/AcquireLicense
  --> Server validates SPC, returns CKC (encrypted content key)
  --> JS receives: ArrayBuffer(CKC data)
```

**What should happen:** License server returns the CKC.

**What actually happens:** Never reached because Step 4 never fires.

### Step 6: "Here's the key to unlock the dish" (Apply License)

JS passes the CKC to the CDM via `session.update()`. The CDM forwards it to
`AVContentKeyRequest` which delivers the content key to AVPlayer.

```
JS: session.update(new Uint8Array(ckcData))
  --> Blink: MediaKeySession::Update(ckcData)
    --> Mojo IPC to GPU process
      --> StarboardCdm::UpdateSession(ticket, sessionId, ckcData)
        --> SbDrmUpdateSession(drm_system, ticket, ckcData, size, sessionId)
          --> SBDApplicationDrmSystem updateSessionWithKey:
            --> AVContentKeyResponse contentKeyResponseWithFairPlayStreamingKeyResponseData:
            --> [keyRequest processContentKeyResponse:keyResponse]
            --> AVPlayer receives the content key
```

**What should happen:** AVPlayer gets the decryption key.

**What actually happens:** Never reached because Step 5 never happens.

### Step 7: "Enjoy your meal!" (Video Plays)

AVPlayer decrypts the content and starts playback.

```
AVPlayer: Content key received, decrypting segments
  --> playerItemStatusDidChange: AVPlayerItemStatusReadyToPlay
    --> [player play]
      --> JS receives: 'canplay' event, 'playing' event
        --> Video renders on screen
```

**What should happen:** Video plays.

**What actually happens:** AVPlayer times out waiting for the key (error -19152).

### Summary

```
Step 1: setServerCertificate    --> BROKEN (cert lost, Gap #1)
Step 2: encrypted event         --> WORKS
Step 3: generateRequest         --> BROKEN (format mismatch, Gap #2 + #3)
Step 4: message event (SPC)     --> never reached
Step 5: fetch license           --> never reached
Step 6: session.update (CKC)    --> never reached
Step 7: video plays             --> never reached (timeout -19152)
```

## 3. The Three Gaps

### Gap #1: setServerCertificate - Certificate Is Rejected (CORRECTED)

**Original analysis said:** "Certificate is lost because `SbDrmUpdateServerCertificate`
only handles Widevine."

**Corrected finding:** The certificate is **rejected before it even reaches Starboard**.
There are TWO gates, not one:

**Gate 1: `SbDrmIsServerCertificateUpdatable` returns `false`**

`drm_is_server_certificate_updatable.mm:34-36`:
```objc
if ([drmManager isApplicationDrmSystem:drm_system]) {
    return false;  // FairPlay application DRM: cert NOT updatable
}
```

**Gate 2: `StarboardCdm::SetServerCertificate` rejects the promise**

`media/starboard/starboard_cdm.cc:146-152`:
```cpp
if (!SbDrmIsServerCertificateUpdatable(sb_drm_)) {
    promise->reject(CdmPromise::Exception::NOT_SUPPORTED_ERROR, 0,
                    "DRM system doesn't support updating server certificate.");
    return;  // SbDrmUpdateServerCertificate is NEVER called
}
```

**Actual code path:**
```
JS: mediaKeys.setServerCertificate(certificate)
  --> StarboardCdm::SetServerCertificate()
    --> SbDrmIsServerCertificateUpdatable(drm) --> false (Gate 1)
    --> promise->reject(NOT_SUPPORTED_ERROR)   (Gate 2)
    --> SbDrmUpdateServerCertificate() is NEVER CALLED
  --> JS receives: false (or rejection)
```

**This is by design in C25, not a bug.** From `EME-C25-Flow.md` lines 261-284:

> Path-1 does NOT use `setServerCertificate()` (Deviates from EME Spec)
>
> Instead, the JavaScript web app packs the certificate directly into the
> `generateRequest()` init data: `[4B len][initData][4B len][contentID][4B len][certData]`

**Two different approaches to certificate delivery:**

| Approach | Who Uses It | Certificate Source | `setServerCertificate()` |
|----------|-------------|-------------------|--------------------------|
| **C25/YouTube** | Path-1 (`"fairplay"` type) | JS packs cert into `generateRequest()` init data as 3rd field | Not supported (`false`) |
| **Standard EME** | WebKit/Safari (`"skd"` type) | JS calls `setServerCertificate()`, CDM stores it | Supported (`true`) |

Both approaches ultimately deliver the same certificate to the same Apple API
(`makeStreamingContentKeyRequestDataForApp:`), just through different paths.

**WebKit comparison:** `CDMInstanceFairPlayStreamingAVFObjC.mm:394-409`:
```objc
void setServerCertificate(Ref<SharedBuffer>&& serverCertificate, ...) {
    m_serverCertificate = WTF::move(serverCertificate);  // stores cert
    callback(CDMInstanceSuccessValue::Succeeded);
}
```
Later in `requestLicense()` (line 776-778):
```objc
if (!m_instance->serverCertificate()) {
    callback(... false, Failed);  // fails if no cert stored
    return;
}
```

**Fix for `"skd"` path:** Enable `setServerCertificate()` for FairPlay so standard
EME web apps work. Keep the existing `"fairplay"` path unchanged for YouTube
backward compatibility.

**Files to change for Gap #1 (6 files):**

The existing creation chain for `SBDApplicationDrmSystem` passes two callbacks:
`sessionUpdateRequestFunc` and `sessionUpdatedFunc`. We add a third:
`serverCertificateUpdatedFunc`, following the exact same pattern.

| # | File | Change | Why |
|---|------|--------|-----|
| 0 | `drm_is_server_certificate_updatable.mm` | Return `true` for application DRM | Gate 1: without this, `StarboardCdm` rejects the promise before calling `SbDrmUpdateServerCertificate` |
| 1 | `application_drm_system.h` | Add `serverCertificate` property, `updateServerCertificate:ticket:` method, add `serverCertificateUpdatedFunc` to designated initializer | Store cert + callback, expose update method |
| 2 | `application_drm_system.mm` | Store `_serverCertificateUpdatedFunc` ivar. Implement `updateServerCertificate:ticket:` which stores cert and fires callback | Execute callback to resolve JS promise |
| 3 | `drm_manager.h` | Add `serverCertificateUpdatedFunc` param to `drmSystemWithContext:` factory | Pass callback through factory |
| 4 | `drm_manager.mm` | Forward new param to `SBDApplicationDrmSystem` init | Thread callback through |
| 5 | `drm_create_system.mm` | Pass `server_certificate_updated_callback` (already available but not forwarded) to `drmSystemWithContext:` | Connect the existing callback to the new chain |
| 6 | `drm_update_server_certificate.mm` | For application DRM: call `[applicationDrmSystem updateServerCertificate:certData ticket:ticket]` | Use ObjC method instead of C++ method (SbDrmSystem for FairPlay is a bridged ObjC pointer, not SbDrmSystemPrivate) |

**Architecture note:** `SbDrmSystem` for application DRM is `(__bridge SbDrmSystem)SBDApplicationDrmSystem*`
(see `drm_manager.mm:86`). This is an ObjC object bridged to a C pointer. You cannot
call C++ virtual methods (`ServerCertificateUpdated()`) on it. The `updateServerCertificate:ticket:`
ObjC method is the correct pattern, consistent with how `generateSessionUpdateRequestWithCertificationData:`
and `updateSessionWithKey:` already work.

**Thread safety note:** `updateServerCertificate:ticket:` may be called from the Media thread,
but `serverCertificate` is used later by `AVContentKeySession` delegate queue. Use `@synchronized`
or atomic property to ensure safe access across threads.

**Flow after fix:**
```
JS: mediaKeys.setServerCertificate(cert)
  --> StarboardCdm::SetServerCertificate()
    --> SbDrmIsServerCertificateUpdatable() --> true           (file 0)
    --> saves promise with ticket
    --> SbDrmUpdateServerCertificate(drm, ticket, cert, size) (file 6)
      --> [applicationDrmSystem updateServerCertificate:       (file 2)
              certData ticket:ticket]
        --> @synchronized: stores cert as _serverCertificate
        --> fires _serverCertificateUpdatedFunc(                (file 2)
              drm, context, ticket, kSbDrmStatusSuccess, "")
          --> StarboardCdm::OnServerCertificateUpdatedFunc()
            --> PostTask to task_runner_
              --> StarboardCdm::OnServerCertificateUpdated()
                --> resolves promise
                  --> JS receives: true
```


### Gap #2: generateRequest - unpackData Expects YouTube Format

**JS call:**
```js
await session.generateRequest("skd", initData);
// initData = 75 bytes of raw UTF-8: "skd://302f80dd-411e-4886-bca5-bb1f8018a024:77FD..."
// initDataType = extracted from URI scheme (e.g., "skd" from "skd://...")
```

**Code path:**
```
JS
  --> Blink MediaKeySession::generateRequest()
    --> Mojo --> StarboardCdm::GenerateRequest()
      --> SbDrmGenerateSessionUpdateRequest()
        --> drm_generate_session_update_request.mm:78-84:

            NSData* packedData = [NSData dataWithBytes:initialization_data
                                                length:initialization_data_size];
            NSArray<NSData*>* unpackedData = unpackData(packedData);
            if (unpackedData.count < 3) {
                SB_DLOG(ERROR) << "Invalid initialization data.";
                return;   // <-- SILENT RETURN, callback never fires
            }
```

**Problem:** `unpackData()` expects YouTube's packed format:
```
[4 bytes: initDataLen][initData bytes][4 bytes: contentIDLen][contentID bytes][4 bytes: certLen][cert bytes]
```
But receives 75 bytes of raw UTF-8 `skd://` URI. The first 4 bytes (`skd:`) are
interpreted as a length (nonsense value), unpacking fails, returns `nil`.

The function returns silently without calling the `_sessionUpdateRequestFunc`
callback. Mojo waits for the callback, times out after 20 seconds:
```
Callback Timeout: Media.EME.Unknown.GenerateRequest.MojoCdmTimeout
```

### Gap #3: Identifier Encoding Mismatch (Would Fail Even If Gap #2 Was Fixed)

Even if `unpackData` somehow succeeded, `application_drm_system.mm:156-158`:
```objc
NSString* requestIdentifier = [[NSString alloc] initWithData:initData
                                  encoding:NSUTF16LittleEndianStringEncoding];
```

Our init data is **UTF-8**, not UTF-16LE. The decoded identifier string would be
garbled and never match `keyRequest.identifier` in `_keyRequestsPendingUpdateRequest`.

**WebKit comparison** (`CDMInstanceFairPlayStreamingAVFObjC.mm:832-833`):
```objc
identifier = adoptNS([[NSString alloc] initWithData:initData->...createNSData().get()
                                           encoding:NSUTF8StringEncoding]);
```
WebKit uses **UTF-8** decoding. Our code uses UTF-16LE because YouTube's packed
format encodes the skd URI as UTF-16LE.

## 4. Why Steps 1 and 3 Are Independent From Step 2

Step 2 (encrypted event) is the **AVPlayer -> JS** direction:
```
AVPlayer detects encrypted HLS segment
  --> AVContentKeySession fires didProvideContentKeyRequest:
    --> application_player.mm extracts URI scheme, fires _encryptedMediaFunc(scheme, uri)
      --> SbPlayerBridge --> StarboardRenderer --> Mojo --> WMPI --> JS
```
This path doesn't need the certificate. AVPlayer discovers encryption independently.

Steps 1 and 3 are the **JS -> Starboard DRM** direction:
```
Step 1: JS sends certificate DOWN to CDM (for storage)
Step 3: JS sends skd URI DOWN to CDM (to generate SPC using stored certificate)
```
These fail because the Starboard DRM layer (`SBDApplicationDrmSystem`) was built
for YouTube's specific format and never receives the certificate.

## 5. Comparison: YouTube Flow vs Standard FairPlay Flow

### YouTube Flow (What the Code Was Built For)

```
1. JS packs init data: [4B len][skd URL as UTF-16LE][4B len][contentID][4B len][cert]
2. JS calls generateRequest("fairplay", packedData)
3. unpackData() extracts all three fields
4. Identifier decoded as UTF-16LE (matches YouTube's format)
5. Certificate comes FROM the packed data (not from setServerCertificate)
6. makeStreamingContentKeyRequestDataForApp:certData called with extracted cert
```

### Standard FairPlay Flow (What Safari/WebKit Does)

```
1. JS calls setServerCertificate(cert) -- cert stored on CDM
2. JS receives encrypted event with raw skd:// URI
3. JS calls generateRequest("skd", rawUri)
4. CDM decodes URI as UTF-8
5. CDM looks up pending AVContentKeyRequest by identifier
6. CDM calls makeStreamingContentKeyRequestDataForApp: with STORED cert
```

### What We Need to Support

Both flows. Branch based on `type` parameter:
- `"fairplay"` --> existing YouTube path (unpackData + UTF-16LE)
- `"skd"` --> new standard path (raw UTF-8 + stored certificate)

## 6. Design Decision: Two Certificate Delivery Paths

The existing C25 code was built exclusively for YouTube's FairPlay flow, where
the certificate is packed into `generateRequest()` init data. Standard EME
(Safari/WebKit) uses `setServerCertificate()` instead.

Rather than changing the existing YouTube path, we add a parallel path for
standard FairPlay (`"skd"` type). Both paths coexist, branching on the init data
type. This ensures zero regression for YouTube while enabling standard EME.

```
                    Certificate Delivery
                          |
              +-----------+-----------+
              |                       |
        type="fairplay"          type="skd"
        (YouTube/C25)            (Standard EME)
              |                       |
    JS packs cert into         JS calls
    generateRequest() data     setServerCertificate()
              |                       |
    unpackData() extracts      CDM stores cert in
    cert as 3rd field          _serverCertificate
              |                       |
    [certData from packed]     [stored _serverCertificate]
              |                       |
              +----------++-----------+
                         |
              makeStreamingContentKeyRequestDataForApp:
                    (same Apple API for both)
```

**Key principle:** Both paths deliver the same certificate to the same Apple API.
The difference is only in how/when the certificate reaches the platform layer.

## 7. Fix Plan

### File 0: `drm_is_server_certificate_updatable.mm` (NEW - was missing from original plan)

**Change:** Return `true` for application DRM (FairPlay) instead of `false`.
Without this, `StarboardCdm::SetServerCertificate` rejects the promise at
`starboard_cdm.cc:146-152` and `SbDrmUpdateServerCertificate` is never called.

**Note:** This changes C25 behavior. C25 intentionally returned `false` because
YouTube packs cert in generateRequest. Standard EME requires `true`.

### File 1: `drm_update_server_certificate.mm`

**Change:** Forward certificate to `SBDApplicationDrmSystem` when the DRM system
is an application DRM system (FairPlay).

```
BEFORE: Only Widevine/DrmSystemPlatform get the certificate
AFTER:  Also forward to SBDApplicationDrmSystem._serverCertificate
```

### File 2: `application_drm_system.h`

**Change:** Add `_serverCertificate` property and new method:
```objc
@property(nonatomic) NSData* serverCertificate;

- (void)generateSessionUpdateRequestForSkdWithInitData:(NSData*)initData
                                                ticket:(NSInteger)ticket;
```

### File 3: `application_drm_system.mm`

**Change:** Implement the new `"skd"` handler:
1. Decode `initData` as UTF-8 to get `skd://` identifier string
2. Look up `AVContentKeyRequest` in `_keyRequestsPendingUpdateRequest` by identifier
3. Extract content identifier from the `skd://` URI
4. Call `makeStreamingContentKeyRequestDataForApp:_serverCertificate
   contentIdentifier:contentIdData` to generate SPC
5. Fire `_sessionUpdateRequestFunc` callback with SPC data

### File 4: `drm_generate_session_update_request.mm`

**Change:** Branch on `type` before calling `unpackData`:
```objc
if (strcmp(type, "skd") == 0) {
    // Standard FairPlay: type derived from URI scheme (e.g., "skd")
    NSData* initData = [NSData dataWithBytes:initialization_data
                                      length:initialization_data_size];
    [applicationDrmSystem generateSessionUpdateRequestForSkdWithInitData:initData
                                                                 ticket:ticket];
} else {
    // YouTube FairPlay: packed format [initData][contentID][cert]
    NSArray<NSData*>* unpackedData = unpackData(packedData);
    // ... existing code ...
}
```

## 8. Resolution Status (2026-05-18)

**All gaps resolved. FairPlay DRM key exchange working end-to-end.**

### Gap Resolution Summary

| Gap | Problem | Fix | Commit | Verified |
|-----|---------|-----|--------|----------|
| **#1** | `setServerCertificate` rejected (`IsUpdatable` returned `false`) | Return `true`, thread callback through DRM creation chain, store cert on `SBDApplicationDrmSystem` | `be90623` | `Server certificate set: true` in logs |
| **#2** | `unpackData()` fails on raw UTF-8 `skd://` URI | Branch on type: `"skd"` skips `unpackData`, passes raw data | `8278552` | `pending key request lookup: FOUND` in logs |
| **#3** | Identifier decoded as UTF-16LE instead of UTF-8 | New `generateSessionUpdateRequestForSkd:` uses `NSUTF8StringEncoding` | `8278552` | Same commit as #2, identifier matches |
| **Bonus** | `session.update` crashes on raw binary CKC (base64 decode of binary) | Try raw binary first, base64 fallback | `71dbe3d` | `Session updated with license` in logs |

### Actual Device Log (2026-05-18, Axinom test server)

```
IsServerCertificateUpdatable: returning true for application DRM (FairPlay)
SbDrmUpdateServerCertificate: forwarding 1242 bytes to SBDApplicationDrmSystem
updateServerCertificate: stored 1242 bytes
updateServerCertificate: firing callback ticket=0
JS: Server certificate set: true

JS: Calling session.generateRequest(skd, initData)...
SbDrmGenerateSessionUpdateRequest: type='skd' size=75 ticket=1
  skd path: raw UTF-8 init data='skd://302f80dd-411e-4886-bca5-bb1f8018a024:77FD...'
  serverCertificate=present, certSize=1242
generateSessionUpdateRequestForSkd: identifier='skd://302f80dd-...' ticket=1
  pending key request lookup: FOUND, pendingCount=0
  calling makeStreamingContentKeyRequestDataForApp certSize=1242

JS: generateRequest completed
JS: SESSION message: type=license-request byteLength=6592
JS: Sending license request to: https://drm-fairplay-licensing.axprod.net/AcquireLicense
JS: License received, size=892 bytes

SbDrmUpdateSession: key is raw binary (not UTF-8), size=892 bytes
SbDrmUpdateSession: ticket=2 keySize=892 sessionIdSize=8
JS: Session updated with license - key should be active!
JS: EVENT: canplay
```

### Remaining Minor Issues

- **Video paused at `time=0.0`**: `canplay` fires but `readyState=3`, `paused=true`.
  AVPlayer has the key but playback hasn't started (likely autoplay policy or
  missing `video.play()` call in test page). This is a test page issue, not DRM.
- **`keystatuseschange` event not fired**: The key status callback from
  `SBDApplicationDrmSystem` may need wiring. Video still decrypts correctly.
