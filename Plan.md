# AVPlayer/URLPlayer HLS Integration for tvOS Cobalt

## Status: ✅ WORKING — Video plays via AVPlayer on Apple TV

---

## Architecture

```
WebMediaPlayerImpl::DoLoad(url)
    ↓ detects HLS (.m3u8 / hls_variant / hls_playlist)
    ↓ skips MultiBufferDataSource
DemuxerManager::CreateDemuxer
    ↓ creates UrlPlayerDemuxer(url) — carries URL via GetMediaUrl()
PipelineImpl::Start(demuxer)
    ↓
StarboardRendererClient::InitializeMojoRenderer(MediaResource*)
    ↓ extracts URL via MediaResource::GetMediaUrl()
    ↓ renderer_extension_->SetSourceUrl(url) — Mojo IPC (ordered)
    ↓
StarboardRendererWrapper::SetSourceUrl → StarboardRenderer::SetSourceUrl
    ↓ stores source_url_
StarboardRenderer::Initialize
    ↓ detects source_url_ → skips stream checks
    ↓ CreatePlayerBridge → URL-based SbPlayerBridge
    ↓
SbUrlPlayerCreate(url) → SBDPlayerManager → SBDApplicationPlayer
    ↓
AVURLAsset → AVPlayerItem → AVPlayer → AVPlayerLayer (punch-out)
    ↓
OnPlayerStatus(kSbPlayerStatePresenting)
    ↓ reports video size via OnVideoNaturalSizeChange + paint_video_hole_frame_cb_
    ↓ web layer creates transparent hole → video visible
```

---

## How to Build & Test

```bash
# Build
ninja -C out/tvos-arm64-device_debug cobalt

# Deploy with HLS test page (no login needed)
./deploy_and_run_cobalt_device.sh \
    --bundle-id 'abhijeet.tvos.org.chromium.chrome.unittests.dev' \
    --device 'Abhijeet-TVOS' \
    --url 'https://people.igalia.com/akandalkar/hls-urlplayer-test.html' \
    --verbose \
    -- --enable-logging=stderr --v=1

# Deploy with YouTube TV (uses MSE/DASH, not HLS — no regression test)
./deploy_and_run_cobalt_device.sh \
    --bundle-id 'abhijeet.tvos.org.chromium.chrome.unittests.dev' \
    --device 'Abhijeet-TVOS' \
    --url 'https://www.youtube.com/tv' \
    --verbose

# Check URL routing in logs
grep "\[URL-ROUTING\]" /tmp/cobalt_*.txt

# Pull crash logs from device
mkdir -p /tmp/tvos_crashlogs && \
xcrun devicectl device copy from \
    --device 'Abhijeet-TVOS' \
    --source '/' \
    --destination /tmp/tvos_crashlogs/ \
    --domain-type systemCrashLogs
```

---

## Commit History

```
7f2c96c media: Route HLS URL via MediaResource::GetMediaUrl()
0b1b218 media: Fix URL player crash and enable AVPlayer HLS playback on tvOS
e9bbeb4 media: Add hls_playlist detection and User-Agent logging
d3e740b tvos: Remove duplicate attachPlayerView call in url_player_create
0d551b5 media: Fix DCHECK in CreatePlayerBridge for URL player path
13b9980 media: Fix SetSourceUrl timing for URL player
999a957 media: Add DoLoad bypass for HLS URLs to skip data source creation
9ef0846 media: Add UrlPlayerDemuxer stub to bypass Chromium pipeline for HLS
428159a media: Fix HLS URL player initialization path
2687a86 media: Bypass demuxer for HLS URLs in DoLoad
8335981 media: Broaden HLS detection to include .m3u8 URLs
f1296f3 media: Use URL-based SbPlayerBridge for HLS in CreatePlayerBridge
b9b4dee media: Detect HLS URL and set source URL on StarboardRendererClient
8f5211f media: Add SetSourceUrl to Starboard renderer Mojo pipeline
f4bc1c0 media: Fix URL player code in SbPlayerBridge for current buildflags
07b24bc tvos: Update url_player_create.mm for current API
6a3a3d5 tvos: Enable URL player build support for HLS playback
```

---

## Key Design Decisions

### URL Routing: MediaResource::GetMediaUrl()
- Added `GetMediaUrl()` virtual method to `MediaResource` (guarded by `USE_STARBOARD_MEDIA`)
- `UrlPlayerDemuxer` carries the URL and implements `GetMediaUrl()`
- `StarboardRendererClient` extracts URL in `InitializeMojoRenderer()` where Mojo is bound
- Mojo message ordering guarantees `SetSourceUrl` arrives before `Initialize`
- No GN dependency violations, no `static_cast` hacks

### UrlPlayerDemuxer: Separate GN target
- `//media/starboard:url_player_demuxer` — breaks circular dependency
- `media/filters:filters` depends on `:url_player_demuxer` (not full `:starboard`)
- Keeps dummy audio/video streams (required by `PipelineImpl` validation)

### Video Hole for Punch-out Mode
- `OnPlayerStatus(kSbPlayerStatePresenting)` reports video size via `OnVideoNaturalSizeChange()`
- `paint_video_hole_frame_cb_.Run(size)` punches transparent hole in web content
- Fallback to 1920x1080 if AVPlayer hasn't reported `presentationSize` yet

### GetAudioConfigurations Crash
- URL player's `SbPlayer` handle is `__bridge`-casted ObjC object, not C++ `SbPlayerPrivate`
- Calling `SbPlayerGetAudioConfiguration()` on it dereferences invalid vtable → crash
- Fixed by skipping `GetAudioConfigurations()` when `source_url_` is set

### FairPlay DRM
- Not needed in Chromium's EME layer — URL player uses Starboard DRM callbacks
- `AVContentKeySession` handles FairPlay natively via `SBDApplicationDrmSystem`

### YouTube HLS Serving
- YouTube decides HLS vs DASH server-side via InnerTube API `clientName`
- `canPlayType("application/x-mpegURL")` already returns "probably" via `SbMediaCanPlayMimeAndKeySystem`
- YouTube TV JS app currently uses `TVHTML5` client → serves DASH
- HLS requires YouTube backend to recognize Cobalt tvOS UA for HLS serving

---

## Files Modified

| File | Purpose |
|------|---------|
| `media/base/media_resource.h/.cc` | `GetMediaUrl()` virtual method |
| `media/starboard/url_player_demuxer.h/.cc` | URL carrier demuxer with `GetMediaUrl()` |
| `media/starboard/BUILD.gn` | Separate `:url_player_demuxer` target |
| `media/filters/BUILD.gn` | Dep on `:url_player_demuxer` |
| `media/filters/demuxer_manager.cc` | Creates `UrlPlayerDemuxer(loaded_url_)` for HLS |
| `media/media_options.gni` | Add `:url_player_demuxer` to subcomponent deps |
| `media/mojo/mojom/renderer_extensions.mojom` | `SetSourceUrl(string)` Mojo method |
| `media/mojo/clients/starboard/starboard_renderer_client.cc/.h` | Extract URL via `GetMediaUrl()`, send via Mojo |
| `media/mojo/services/starboard/starboard_renderer_wrapper.cc/.h` | Forward `SetSourceUrl` to renderer |
| `media/starboard/starboard_renderer.cc/.h` | URL player path in `Initialize()` + `CreatePlayerBridge()` |
| `media/starboard/sbplayer_bridge.cc/.h` | URL-based constructor, buildflags fixes |
| `starboard/tvos/shared/configuration_public.h` | `SB_HAS_PLAYER_WITH_URL`, `SB_URL_PLAYER_INCLUDE_PATH` |
| `starboard/tvos/shared/BUILD.gn` | URL player source files |
| `starboard/tvos/shared/media/url_player_create.mm` | `SbUrlPlayerCreate()` implementation |
| `starboard/tvos/shared/media/application_player.mm` | AVPlayer lifecycle, `[player play]` fix |
| `starboard/tvos/shared/application_darwin.mm` | `attachPlayerView` logging |
| `cobalt/shell/browser/shell_platform_delegate_ios.mm` | `playerContainerView` sized to screen |
| `third_party/blink/.../web_media_player_impl.cc` | HLS detection in `DoLoad()` |

---

## Next Steps

1. YouTube TV JS app needs InnerTube client config to serve HLS for tvOS
2. FairPlay DRM testing with encrypted HLS content
3. Clean up debug logging (`[URL-ROUTING]`, `[AVPlayer]`, entry/exit logs)
4. Seek/play/pause integration testing via HTML5 media API
