Read ResearchReport.md for full context on the AVPlayer/URLPlayer HLS integration for tvOS Cobalt.

## Starting Point

Unprotected HLS playback works on the Avplayer branch. The last commit before DRM work is c4e3c98 (docs: Add Plan.md). At this point:
- `<video src="https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8">` plays video via AVPlayer
- URL routing: WebMediaPlayerImpl -> UrlPlayerDemuxer -> StarboardRenderer -> SbUrlPlayerCreate -> AVPlayer
- Video hole punch-out works, video visible on Apple TV

## Goal

Add FairPlay DRM support so `<video src="encrypted.m3u8">` plays FairPlay-encrypted HLS content. The encrypted test stream is:
- `https://media.axprod.net/TestVectors/Cmaf/protected_1080p_h264_cbcs/manifest.m3u8`
- Uses `#EXT-X-KEY:METHOD=SAMPLE-AES,URI="skd://...",KEYFORMAT="com.apple.streamingkeydelivery"`
- License server: `https://drm-fairplay-licensing.axprod.net/AcquireLicense` (Axinom, JWT token in fairplay-urlplayer-test.html)

## Key Constraint

AVPlayer handles HLS natively -- it discovers encryption, fetches segments, decodes video. The DRM key exchange must flow through Chromium's EME/CDM layer because the **web app** (e.g. YouTube TV JS) controls the license server URL and certificate. The C25 Cobalt design was:

1. AVPlayer encounters `#EXT-X-KEY` -> `AVContentKeySession` fires `didProvideContentKeyRequest:`
2. `_encryptedMediaFunc` callback notifies Cobalt -> JS gets `encrypted` event
3. JS calls EME: `requestMediaKeySystemAccess('com.apple.fps')` -> `createMediaKeys()` -> `setMediaKeys()`
4. This creates `SbDrmSystem` via `SbDrmCreateSystem("com.apple.fps")` -> `SBDApplicationDrmSystem`
5. `SbUrlPlayerSetDrmSystem()` attaches DRM to player -> `setDrmSystem:` -> `_drmSystem.keySession = _keySession`
6. Pending key requests processed: `processKeyRequest:` -> `SBDApplicationDrmSystem` generates SPC
7. SPC sent up via `SbDrmSessionUpdateRequestFunc` callback -> CDM -> JS `message` event
8. JS sends SPC to license server -> gets CKC -> `session.update(ckc)`
9. CKC flows down via `SbDrmSessionUpdate` -> `SBDApplicationDrmSystem` -> `AVContentKeyResponse` -> AVPlayer decrypts

## What to study

Trace the COMPLETE round-trip carefully through these files. For each step, note the exact function, file:line, and how data flows to the next step:

### tvOS Starboard DRM (the native side):
- `starboard/tvos/shared/media/application_drm_system.h/.mm` -- SPC generation, CKC handling, `processKeyRequest:`
- `starboard/tvos/shared/media/application_player.mm` -- `setDrmSystem:`, pending key queue, `contentKeySession:didProvideContentKeyRequest:`
- `starboard/tvos/shared/media/drm_create_system.mm` -- `SbDrmCreateSystem` routing for "com.apple.fps"
- `starboard/tvos/shared/media/drm_manager.h/.mm` -- DRM system lifecycle, bridge casting
- `starboard/tvos/shared/media/url_player_set_drm_system.mm` -- `SbUrlPlayerSetDrmSystem`
- `starboard/tvos/shared/media/url_player.h` -- URL player API design (C25 era)

### Chromium CDM layer (bridges Starboard <-> Blink):
- `media/starboard/sbplayer_bridge.cc` -- `SetDrmSystem()`, `EncryptedMediaInitDataEncounteredCB`
- `media/starboard/starboard_renderer.cc` -- `SetCdm()`, `OnEncryptedMediaInitDataEncountered`, `CreatePlayerBridge`
- `media/mojo/mojom/renderer_extensions.mojom` -- Mojo interfaces
- `media/mojo/clients/starboard/starboard_renderer_client.cc` -- renderer process side
- `media/mojo/services/starboard/starboard_renderer_wrapper.cc` -- GPU process side

### Blink EME layer (JS <-> CDM):
- `third_party/blink/renderer/platform/media/web_media_player_impl.cc` -- `OnEncryptedMediaInitData`, `OnWaiting`
- `third_party/blink/renderer/platform/media/key_system_config_selector.cc` -- encryption scheme negotiation
- `components/cdm/renderer/fairplay_key_system_info.cc` -- FairPlay key system registration

### Starboard DRM API (the SbDrm* functions that bridge C++ <-> ObjC):
- Look for `SbDrmGenerateSessionUpdateRequest`, `SbDrmUpdateSession`, `SbDrmSessionUpdateRequestFunc`, `SbDrmSessionUpdatedFunc` -- these are the callback functions that carry SPC/CKC data between layers

## Deliverable

1. **Complete sequence diagram** of the FairPlay key exchange: AVPlayer -> native -> Starboard -> CDM -> Mojo -> JS -> license server -> JS -> Mojo -> CDM -> Starboard -> native -> AVPlayer. Show file:line for each step.
2. **Gap analysis**: Where exactly does the current implementation break? What callbacks are not connected?
3. **Implementation plan**: Specific code changes needed, ordered by dependency. Keep it simple -- minimum changes to make the existing C25 native DRM code work with Chromium's CDM layer.

Do NOT implement anything -- only research and plan.
Not all details in ResearchReport.md or this props are 100% accurate, please do visit files and verify.
We need simple, clean and minimal changes. Further complex parts can be implemented later on as followups.
