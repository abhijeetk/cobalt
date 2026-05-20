# Design Doc: URL Player Phase 3 -- Media Control & Pipeline Integration

**Context:** Phase 1 and 2 focused on architecture and DRM. Phase 3 ensures that the URL Player (AVPlayer) behaves as a fully integrated HTML5 Media Element within the Chrobalt pipeline.

## 1. Objectives
*   **Command Mapping:** Bind HTML5 Media API calls (`play()`, `pause()`, `seek()`, `playbackRate`, `volume`, `muted`) to native `AVPlayer` methods.
*   **State Synchronization:** Map native `AVPlayerItem` status changes back to Chromium `ReadyState` and `NetworkState`.
*   **Buffering & Progress:** Accurately report `buffered` ranges and `currentTime` from the native player to the web app.
*   **QoE & Metrics:** Forward hardware-reported dropped frames and quality-of-service stats to Chromium's `PipelineStatistics`.

## 2. Architecture: The Control Loop
The integration uses a **Dual-Proxy** model over Mojo:

1.  **StarboardRendererClient (Renderer):** Intercepts `WebMediaPlayerImpl` commands and sends them over the `StarboardRendererExtension` Mojo interface.
2.  **StarboardRenderer (GPU):** Receives Mojo calls and translates them into `SbPlayer` (Starboard) calls, which `SbPlayerBridge` then applies to the native `AVPlayer`.

## 3. Key Implementation Areas

### A. Playback Controls (The Command Path)

All playback commands flow through the standard `mojom::Renderer` interface (not the extension). The pipeline rate controls both play and pause:

**Play/Pause via SetPlaybackRate:**
```
JS video.play() / video.pause()
  -> HTMLMediaElement::UpdatePlayState()
    -> WebMediaPlayerImpl::Play() [SetPlaybackRate(1.0)]
       WebMediaPlayerImpl::Pause() [SetPlaybackRate(0.0)]
      -> PipelineController::SetPlaybackRate()
        -> PipelineImpl::RendererWrapper::SetPlaybackRate()
          [only forwarded when state_ == kPlaying]
          -> MojoRenderer::SetPlaybackRate() -> Mojo IPC
            -> StarboardRendererWrapper::SetPlaybackRate()
              -> StarboardRenderer::SetPlaybackRate()
                -> SbPlayerBridge::SetPlaybackRate()
                  -> SbPlayerSetPlaybackRate()
                    -> ApplicationPlayer.playbackRate = rate
                      -> AVPlayer.rate = rate (0.0=pause, 1.0=play)
```

**Key discovery:** In C25, playback was controlled exclusively via `SetPlaybackRate()`. The native `[player play]` was never called in ReadyToPlay. A `[player play]` call was added during the Chrobalt port as a workaround (commit `be4f6302beb28`), but this bypassed Chromium's pipeline, creating a rate mismatch where the native AVPlayer was playing at rate=1 but Chromium thought rate=0. This made `video.pause()` a no-op because StarboardRenderer saw rate as "already 0" and skipped forwarding. Fix: removed `[player play]`, removed rate-unchanged guards, let pipeline be the sole authority.

**Seek:**
```
JS video.currentTime = X
  -> WebMediaPlayerImpl::DoSeek()
    -> PipelineController::Seek()
      -> StarboardRenderer::StartPlayingFrom(time)
        -> SbPlayerBridge::Seek()
          -> SbPlayerSeek()
            -> AVPlayer.seekToTime:toleranceBefore:toleranceAfter:
```

Seek works end-to-end via the standard pipeline path. No URL-player-specific changes were needed.

**Volume/Mute:**
```
JS video.muted = true / video.volume = X
  -> HTMLMediaElement -> WebMediaPlayerImpl::SetVolume(0.0 / X)
    -> PipelineController::SetVolume()
      -> StarboardRenderer::SetVolume()
        -> SbPlayerBridge::SetVolume()
          -> SbPlayerSetVolume()
            -> AVPlayer.volume = X
```

**Key discovery:** The `<video muted>` attribute sets volume=0 during pipeline init, BEFORE the AVPlayer is created. The `ApplicationPlayer.volume = 0` call becomes a no-op because `_player` is nil. AVPlayer then starts with default volume=1. Fix: re-apply stored `volume_` in `StarboardRenderer::OnPlayerStatus(kSbPlayerStatePresenting)` after AVPlayer is confirmed ready.

### B. State Mapping (The Observation Path)

**ReadyState progression:**
*   `kSbPlayerStateInitialized` -> pipeline init callback, `readyState=1` (HAVE_METADATA)
*   `kSbPlayerStatePrerolling` -> no readyState change
*   `kSbPlayerStatePresenting` -> `BUFFERING_HAVE_ENOUGH` -> `readyState=4` (HAVE_ENOUGH_DATA)

**Key discovery:** `BUFFERING_HAVE_ENOUGH` triggers `SetReadyState(CanPlayThrough() ? kHaveEnoughData : kHaveFutureData)`. For URL players, `CanPlayThrough()` returned false because the DoLoad bypass skips DataSource creation, so `buffered_data_source_host_->CanPlayThrough()` had nothing to query. This capped readyState at 3, preventing autoplay (which requires readyState=4). Fix: added `kUrlPlayerDemuxer` to the DemuxerType enum and return true from `CanPlayThrough()` for URL players since AVPlayer manages its own buffering.

**Autoplay:**
```
HTMLMediaElement::SetReadyState(kHaveEnoughData)
  -> autoplay_policy_->RequestAutoplayByAttribute()
    -> paused_ = false
    -> UpdatePlayState()
      -> web_media_player_->Play()  [triggers SetPlaybackRate(1.0)]
```

Autoplay only triggers at `readyState=4`. The `kUrlPlayerDemuxer` + `CanPlayThrough()` fix was essential.

### C. Buffering & Timeline

**Buffered Ranges (GPU -> Renderer -> JS):**
```
AVPlayer.loadedTimeRanges
  -> SbPlayerBridge::GetUrlPlayerBufferedTimeRanges()
    -> StarboardRenderer::GetMediaTime() [periodic poll]
      -> buffered_ranges_cb_
        -> StarboardRendererWrapper::OnBufferedTimeRangesChange()
          -> Mojo IPC (OnBufferedTimeRangesChange in renderer_extensions.mojom)
            -> StarboardRendererClient::OnBufferedTimeRangesChange()
              -> DemuxerManager::SetBufferedTimeRanges()
                -> UrlPlayerDemuxer::SetBufferedTimeRanges()
                  -> DemuxerHost::OnBufferedTimeRangesChanged()
                    -> PipelineImpl shared_state_.buffered_time_ranges
                      -> JS video.buffered
```

Polled in `GetMediaTime()` which runs on a periodic timer via `MojoRendererService`.

**CurrentTime (already working):**
```
MojoRendererService timer -> renderer_->GetMediaTime()
  -> StarboardRenderer::GetMediaTime()
    -> SbPlayerBridge::GetInfo()
      -> SbPlayerGetInfo -> applicationPlayer.currentMediaTime
        -> AVPlayer.currentTime
  -> OnTimeUpdate(time, max_time) via Mojo
    -> MojoRenderer::media_time_interpolator_.SetBounds()
      -> JS video.currentTime (interpolated between polls)
```

**Duration (GPU -> Mojo -> DemuxerHost -> Pipeline -> JS):**
```
AVPlayer.duration
  -> SbPlayerBridge::GetDuration()
    -> StarboardRenderer::OnPlayerStatus(kPresenting)
      -> duration_change_cb_
        -> StarboardRendererWrapper::OnDurationChange()
          -> Mojo IPC (OnDurationChange in renderer_extensions.mojom)
            -> StarboardRendererClient::OnDurationChange()
              -> DemuxerManager::SetDuration()
                -> UrlPlayerDemuxer::SetDuration()
                  -> DemuxerHost::SetDuration()
                    -> PipelineImpl::OnDurationChange()
                      -> WebMediaPlayerImpl::OnDurationChange()
                        -> JS video.duration
```

**Key discovery:** The `OnDurationChange` Mojo callback existed in `renderer_extensions.mojom` but was a TODO stub in `StarboardRendererClient`. Duration normally comes from the demuxer via `DemuxerHost::SetDuration()`. For URL players, the `UrlPlayerDemuxer` doesn't parse media, so duration must be pushed from the GPU side. We added `UrlPlayerDemuxer::SetDuration()` and `SetBufferedTimeRanges()` which call through to the `DemuxerHost`.

### D. Natural Size & Rendering
*   **Punch-Out View:** Forward `presentationSize` from the GPU process to the Renderer via `OnVideoNaturalSizeChange` to ensure the "Video Hole" (transparency) is correctly sized. Already working.

### E. URL Player Demuxer (kUrlPlayerDemuxer)

The `UrlPlayerDemuxer` is a stub demuxer that satisfies Chromium's pipeline requirements without actually demuxing. AVPlayer handles all network, demuxing, and decoding natively.

*   **DemuxerType:** `kUrlPlayerDemuxer = 8` (added to enum, mojom, traits, histograms)
*   **GetMediaUrl():** Returns the HLS URL for `StarboardRendererClient` to pass via `SetSourceUrl`
*   **SetDuration() / SetBufferedTimeRanges():** Push data from GPU side to `DemuxerHost`
*   **Initialize():** Immediately succeeds (no media to parse)
*   **GetAllStreams():** Returns stub audio+video streams with minimal configs

Previously used `kUnknownDemuxer` which was ambiguous and made `CanPlayThrough()` return false.

## 4. Timing Patterns: Pre-AVPlayer Init

Several properties are set by Chromium BEFORE the native AVPlayer is created. This is a recurring pattern for URL players because player creation is deferred to `CreatePlayerBridge()` which runs after Mojo setup.

| Property | When Set by Chromium | When AVPlayer Created | Fix |
|----------|---------------------|----------------------|-----|
| Volume (muted) | Pipeline init | After `CreatePlayerBridge()` in `OnSbWindowHandleReady` | Re-apply in `OnPlayerStatus(kPresenting)` |
| PlaybackRate | Pipeline init (rate=0, paused) | Same | Rate stored in `playback_rate_`, applied when pipeline calls `Play()` |

Any new properties that need to survive across this gap should follow the same pattern: store in `StarboardRenderer`, re-apply in `OnPlayerStatus(kSbPlayerStatePresenting)`.

## 5. End-of-Stream (EOS) -- Already Working

```
AVPlayerItemDidPlayToEndTimeNotification
  -> ApplicationPlayer::playerItemDidReachEnd:
    -> updatePlayerState:kSbPlayerStateEndOfStream
      -> SbPlayerBridge::PlayerStatusCB -> OnPlayerStatus()
        -> StarboardRenderer::OnPlayerStatus(kSbPlayerStateEndOfStream)
          -> client_->OnEnded()
            -> StarboardRendererClient::OnEnded() -> Mojo
              -> WebMediaPlayerImpl::OnEnded() [sets ended_=true]
                -> HTMLMediaElement::TimeChanged()
                  -> ScheduleNamedEvent('ended')
                    -> JS 'ended' event
```

No gaps. Verified by code trace (not yet device-tested to end of stream).

## 6. Error Handling -- Already Working

```
AVPlayerItemStatusFailed (KVO on currentItem.status)
  -> ApplicationPlayer::playerItemStatusDidChange
    -> updatePlayerError:kSbPlayerErrorDecode message:error.description
      -> SbPlayerBridge::PlayerErrorCB -> OnPlayerError()
        -> StarboardRenderer::OnPlayerError()
          -> NotifyError(PIPELINE_ERROR_DECODE)
            -> client_->OnError(status)
              -> StarboardRendererClient::OnError() -> Mojo
                -> WebMediaPlayerImpl::OnError()
                  -> SetNetworkState(kNetworkStateDecodeError)
                    -> JS 'error' event (video.error.code)
```

Error mapping: `kSbPlayerErrorDecode` -> `PIPELINE_ERROR_DECODE`, `kSbPlayerErrorCapabilityChanged` -> `PIPELINE_ERROR_DECODE`. No gaps.

## 7. Remaining Items

| Item | Status | Priority |
|------|--------|----------|
| NetworkState transitions | Not mapped (stays at LOADING=2) | Low -- YouTube app unlikely to depend on it |
| QoE/dropped frames | Flows via `GetMediaTime()` -> `OnStatisticsUpdate` | Low -- needs device verification |

## 8. Challenges
*   **Mojo Latency:** Minimizing lag between GPU-reported time and Renderer-side queries. Mitigated by `MediaTimeInterpolator` in `MojoRenderer`.

## 6. Completed Tasks

| Task | Commit | Description |
|------|--------|-------------|
| Play/Pause Pipeline Fix | `1ec791ec58714` | Removed `[player play]` from ReadyToPlay (not in C25). Removed rate-unchanged guards. Added `[Phase3-Play-Pause]` trace logging across all 8 layers. Added test page with tvOS remote navigation. |
| Autoplay + kUrlPlayerDemuxer | `08a8353950213` | Added `kUrlPlayerDemuxer` enum. Fixed `CanPlayThrough()` for URL players. readyState now reaches 4, autoplay triggers. |
| Duration + CurrentTime | `3f68b26b40efe` | Implemented `OnDurationChange` Mojo callback (was TODO stub). Added `UrlPlayerDemuxer::SetDuration()`. CurrentTime already worked via periodic timer. |
| Buffered Ranges + Seek | `5a1c75989ba04` | Added `OnBufferedTimeRangesChange` Mojo message. Polled in `GetMediaTime()`. Seek already worked. |
| Muted/Volume Init Fix | `pending` | Re-apply stored `volume_` in `OnPlayerStatus(kPresenting)` after AVPlayer is created. |
