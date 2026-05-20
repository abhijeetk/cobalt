# Design Doc: URL Player Phase 3 — Media Control & Pipeline Integration

**Context:** Phase 1 and 2 focused on architecture and DRM. Phase 3 ensures that the URL Player (AVPlayer) behaves as a fully integrated HTML5 Media Element within the Chrobalt pipeline.

## 1. Objectives
*   **Command Mapping:** Bind HTML5 Media API calls (`play()`, `pause()`, `seek()`, `playbackRate`) to native `AVPlayer` methods.
*   **State Synchronization:** Map native `AVPlayerItem` status changes back to Chromium `ReadyState` and `NetworkState`.
*   **Buffering & Progress:** Accurately report `buffered` ranges and `currentTime` from the native player to the web app.
*   **QoE & Metrics:** Forward hardware-reported dropped frames and quality-of-service stats to Chromium's `PipelineStatistics`.

## 2. Architecture: The Control Loop
The integration uses a **Dual-Proxy** model over Mojo:

1.  **StarboardRendererClient (Renderer):** Intercepts `WebMediaPlayerImpl` commands and sends them over the `StarboardRendererExtension` Mojo interface.
2.  **StarboardRenderer (GPU):** Receives Mojo calls and translates them into `SbPlayer` (Starboard) calls, which `SbPlayerBridge` then applies to the native `AVPlayer`.

## 3. Key Implementation Areas

### A. Playback Controls (The Command Path)
*   **Seek:** Map `WebMediaPlayerImpl::Seek` to `SbPlayerSeek`. Handle `AVPlayer`'s asynchronous `seekToTime:completionHandler:` to correctly trigger the `OnPipelineSeeked` callback in the Renderer.
*   **Rate:** Map `setRate()` to `AVPlayer.rate`.

### B. State Mapping (The Observation Path)
Map native `AVPlayer` statuses to Chromium `ReadyState`:
*   `AVPlayerItemStatusReadyToPlay` → `kReadyStateHaveEnoughData`
*   `AVPlayerTimeControlStatusWaitingToPlayAtSpecifiedRate` → `kReadyStateHaveCurrentData` (Buffering)
*   `AVPlayerTimeControlStatusPlaying` → `kReadyStateHaveEnoughData`

### C. Buffering & Timeline
*   **Buffered Ranges:** Poll `AVPlayerItem.loadedTimeRanges` in the GPU process and send to the Renderer via Mojo to populate `WebMediaPlayerImpl::Buffered()`.
*   **Time Tracking:** High-frequency polling of `AVPlayer.currentTime` for the Renderer-side `GetMediaTime` call.

### D. Natural Size & Rendering
*   **Punch-Out View:** Forward `presentationSize` from the GPU process to the Renderer via `OnVideoNaturalSizeChange` to ensure the "Video Hole" (transparency) is correctly sized.

## 4. Challenges
*   **Mojo Latency:** Minimizing lag between GPU-reported time and Renderer-side queries.
*   **End-of-Stream (EOS):** Mapping `AVPlayerItemDidPlayToEndTimeNotification` to Chromium's `OnEnded()`.
*   **Error Mapping:** Translating native CoreMedia errors (e.g., `-19152`) to Chromium `PipelineStatus`.

## 5. Completed Tasks

| Task | Commit | Description |
|------|--------|-------------|
| Play/Pause Pipeline Fix | `1ec791ec58714` | Fixed rate mismatch between native AVPlayer and Chromium pipeline. Removed direct `[player play]` from ReadyToPlay handler (was bypassing pipeline, added during Chrobalt port, not present in C25). Removed rate-unchanged early-out guards in StarboardRenderer and ApplicationPlayer so SetPlaybackRate always reaches AVPlayer. Added `[Phase3-Play-Pause]` trace logging across all 8 pipeline layers (WebMediaPlayerImpl, PipelineImpl, MojoRenderer, StarboardRendererWrapper, StarboardRenderer, SbPlayerBridge, SbPlayerSetPlaybackRate, ApplicationPlayer). Added playback control test page with tvOS remote navigation (play, pause, seek, rate, mute buttons with tabindex focus management). |
| Autoplay Fix + kUrlPlayerDemuxer | `08a8353950213` | Added `kUrlPlayerDemuxer` to DemuxerType enum (value 8) replacing `kUnknownDemuxer` for URL players. Fixed autoplay by returning true from `CanPlayThrough()` for URL player demuxer, allowing readyState to reach `kHaveEnoughData` (4) which triggers `RequestAutoplayByAttribute()`. Root cause: DoLoad bypass skips DataSource creation for HLS URLs, so `buffered_data_source_host_->CanPlayThrough()` returned false, capping readyState at 3. Updated mojom, traits, histograms. |
| Duration + CurrentTime Reporting | `pending` | Wired duration from native AVPlayer through 7-layer callback chain to JS. OnDurationChange Mojo callback was a TODO stub. CurrentTime already flows via MojoRendererService periodic timer polling StarboardRenderer::GetMediaTime(). |
