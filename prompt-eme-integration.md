# Prompt: EME/FairPlay DRM Integration for URL Player (Path-1) on tvOS

You are a Senior Software Engineer working on the Cobalt browser (a Chromium-based browser for embedded platforms). Your task is to implement FairPlay DRM support for the HLS URL Player (AVPlayer) on tvOS.

## Context

Read these two documents thoroughly before writing any code:

1. **`/Users/abhijeet/code/cobalt-github/src/EME-C25-Flow.md`** -- Contains the complete FairPlay EME flow traced through the C25 (legacy) Cobalt codebase, including the simplified overview, sequence diagrams, key request lifecycle, certificate vs license handling, and the complete callback chain with file:line references.

2. **`/Users/abhijeet/code/cobalt-github/src/ResearchReport.md`** -- Contains the gap analysis (G1-G9), the 8-task implementation plan with exact file:line references, task dependency graph, and verification plan.

## What Already Works

- HLS URL routing: `.m3u8` detection in `DemuxerManager`, `UrlPlayerDemuxer` stub, `SetSourceUrl` Mojo IPC, `SbUrlPlayerCreate` -- the URL reaches AVPlayer and unencrypted HLS plays.
- `SBDApplicationDrmSystem` and `SBDApplicationPlayer` (tvOS Starboard layer) -- the native FairPlay key exchange code is already implemented and tested in C25. We do not modify these files.
- `SbPlayerBridge` URL constructor, `SetDrmSystem`, `EncryptedMediaInitDataEncounteredCB` -- already exist.
- `FairplayKeySystemInfo` in `components/cdm/renderer/` -- already registers FairPlay key systems.

## What Needs to Be Implemented

The gap is the **Mojo/Chromium plumbing** between the GPU process (where AVPlayer lives) and the renderer process (where JavaScript EME lives). There are 8 tasks described in `ResearchReport.md` Section 4.

## Implementation Rules

1. **Read before writing.** For every file you modify, read the current state first. Understand the existing patterns.
2. **One task at a time.** Implement each task separately. Build and verify after each task.
3. **Build after every change.** Run `autoninja -C out/tvos-arm64-device_debug cobalt -j50` and verify zero errors before moving to the next task.
4. **Do not modify args.gn or build config** without explicit permission.
5. **Follow existing patterns.** When adding a new method to an interface (e.g., `RendererClient`), look at how existing methods like `OnWaiting` are declared, implemented, and forwarded through the chain.
6. **No hacky solutions.** No hardcoded URLs, no test-only code in production paths, no `#ifdef` hacks. Every change should be production-quality.
7. **Platform Guard Strategy.** The URL player path is specifically developed for tvOS. The code it touches spans two areas: upstream Chromium code and Cobalt's Starboard media layer. We use a different guard in each area, depending on what build flags are available there.

   | Where | Guard | Why |
   |-------|-------|-----|
   | **Non-Starboard code** (upstream Chromium, Cobalt content layer fork in `//cobalt`, `//components`, `//third_party/blink`, etc.) | `BUILDFLAG(IS_IOS_TVOS) && BUILDFLAG(USE_STARBOARD_MEDIA)` | `USE_STARBOARD_MEDIA` alone is not enough because it is true for some Cobalt variants, not just tvOS. Adding `IS_IOS_TVOS` limits it to the right platform. |
   | **Starboard code** (`media/starboard/`, etc.) | `#if SB_HAS(PLAYER_WITH_URL)` | This flag would be defined only by the tvOS platform (in `starboard/tvos/shared/configuration_public.h`) and directly expresses the platform capability we need. |
   | **Mojo IPC definitions** (`media/mojo/`) | Always present | Mojom's `EnableIf` cannot target tvOS specifically, so these methods are always declared. They are harmless if never called, and the upstream guard ensures they are never called on non-tvOS builds. |
   | **Enum declarations** (`media/base/eme_constants.h`, etc.) | `BUILDFLAG(IS_IOS_TVOS) && BUILDFLAG(USE_STARBOARD_MEDIA)` | Guard new enum values (e.g., `SINF`, `SKD`, `FAIRPLAY`) so non-tvOS platforms never see them. This avoids touching switch statements in platform-specific files (android, fuchsia, windows, chromecast). Files compiled for tvOS that switch on the enum must add guarded cases. |

   The first guard prevents the URL player path from being triggered. The second guard prevents the URL player implementation from being compiled. We need both because `BUILDFLAG(IS_IOS_TVOS)` is available across the whole codebase, while `SB_HAS(PLAYER_WITH_URL)` is only available in Starboard headers.
8. **Do not add Claude as co-author** in commit messages.
9. **Save deployment logs to a file** (`> /tmp/cobalt_debug.log`) then search separately. Do not pipe deployment output through grep directly.

## Task Execution Order

Start with the independent tasks (green in the dependency graph), then the dependent ones:

### Round 1 (Independent -- can be done in any order)

**Task 1: Add `EmeInitDataType::FAIRPLAY`**
- Read `ResearchReport.md` Task 1 for the complete list of 11 files.
- Add `FAIRPLAY` to the `EmeInitDataType` enum in `media/base/eme_constants.h`.
- Update every `switch` statement on `EmeInitDataType` across the codebase.
- Map `"fairplay"` to `FAIRPLAY` in `encrypted_media_utils.cc`.
- Build and verify.

**Task 2: Native Signal Discovery Fix**
- Read `ResearchReport.md` Task 2 and `EME-C25-Flow.md` "Key Request Lifecycle" section.
- In `application_player.mm:791-803`, ensure `_encryptedMediaFunc` fires even when `_drmSystem` is nil (the `else` branch currently only queues without notifying Cobalt).
- Build and verify.

**Task 3: Bridge Thread Safety**
- Read `ResearchReport.md` Task 3.
- In `sbplayer_bridge.cc:593`, wrap the callback invocation in `task_runner_->PostTask()` to ensure it runs on the correct thread.
- Build and verify.

**Task 4: Mojo Interface**
- Read `ResearchReport.md` Task 4.
- Add `OnEncryptedMediaInitDataEncountered(string type, array<uint8> data)` to `StarboardRendererClientExtension` in `renderer_extensions.mojom`. (Note: this declaration may already exist from earlier work -- check first. If it exists, verify the signature matches.)
- Build and verify. Expect compilation errors from unimplemented pure virtuals -- that is expected and will be fixed in Tasks 6 and 7.

**Task 8: SetCdm calls SetDrmSystem for URL Player**
- Read `ResearchReport.md` Task 8 and `EME-C25-Flow.md` "CDM / DRM: Lazy Attachment" section.
- In `StarboardRenderer::SetCdm` (or `StarboardUrlRenderer::SetCdm`), after storing `drm_system_`, call `player_bridge_->SetDrmSystem(drm_system_)` if the player bridge exists and is a URL player.
- This connects `AVContentKeySession` to the DRM system and drains pending key requests.
- Build and verify.

### Round 2 (Depends on Task 4)

**Task 6: GPU Side -- Signal Discovery**
- Read `ResearchReport.md` Task 6.
- In `StarboardRenderer::OnEncryptedMediaInitDataEncountered` (currently a TODO at line 548), call the wrapper callback to send the signal via Mojo.
- In `StarboardRendererWrapper`, forward the signal via `client_extension_remote_->OnEncryptedMediaInitDataEncountered()`.
- Build and verify.

### Round 3 (Depends on Tasks 1 + 4 + 6)

**Task 7: Renderer Side -- Pipe to WebMediaPlayerImpl**
- Read `ResearchReport.md` Task 7. This is the most complex task.
- Add `OnEncryptedMediaInitData(EmeInitDataType type, const std::vector<uint8_t>& data)` to `RendererClient` (`media/base/renderer_client.h`).
- Add the same to `Pipeline::Client` (`media/base/pipeline.h`).
- Implement forwarding in `PipelineImpl` (follow the pattern of `OnWaiting`).
- In `StarboardRendererClient`, implement the Mojo handler: map `"fairplay"` string to `EmeInitDataType::FAIRPLAY`, then call `client_->OnEncryptedMediaInitData()`.
- Build and verify.

### Round 4 (Optional -- verify first if needed)

**Task 5: Starboard Key System Aliases**
- Read `ResearchReport.md` Task 5.
- Check if `FairplayKeySystemInfo::ShouldUseBaseKeySystemName()` already handles the `"com.apple.fps"` to `"com.youtube.fairplay"` substitution. If yes, skip this task.
- If not, add alias support in `drm_create_system.mm`.

## Verification

After all tasks are complete:
1. Build: `autoninja -C out/tvos-arm64-device_debug cobalt -j50`
2. Deploy to Apple TV with the FairPlay test page:
   ```bash
   ./deploy_and_run_cobalt_device.sh \
     --bundle-id 'abhijeet.tvos.org.chromium.chrome.unittests.dev' \
     --device 'Abhijeet-TVOS' \
     --url 'https://people.igalia.com/akandalkar/fairplay/fairplay-urlplayer-test.html' \
     --verbose -- --enable-logging=stderr --v=1 \
     2>&1 > /tmp/cobalt_eme_test.log
   ```
3. Check logs: `grep "OnEncryptedMediaInitData\|fairplay\|FAIRPLAY\|SetDrmSystem\|encrypted\|license\|SPC\|CKC" /tmp/cobalt_eme_test.log`
4. Expected: `encrypted` event fires in JS, license exchange completes, video plays.

## Key Architecture Reminder

```
Renderer Process (JS + EME)          GPU Process (AVPlayer + DRM)
================================     ================================
JS 'encrypted' event                 AVPlayer detects FairPlay
       ^                                    |
       |                             SBDApplicationPlayer
WebMediaPlayerImpl                   _encryptedMediaFunc callback
       ^                                    |
Pipeline::Client                     SbPlayerBridge (PostTask!)
       ^                                    |
PipelineImpl::RendererWrapper        StarboardRenderer
       ^                                    |
RendererClient                       StarboardRendererWrapper
       ^                                    |
StarboardRendererClient  <-- Mojo --> client_extension_remote_
```

The `encrypted` event data flows RIGHT to LEFT (GPU to Renderer).
The `SetCdm` / DRM attachment flows LEFT to RIGHT (Renderer to GPU).
