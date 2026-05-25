# Analysis: Chromium HLS Demuxer + AVSBDL + FairPlay POC for tvOS

*Author: Cobalt Engineer*
*May 2026*

> **Scope**: This document covers **only** the stream-based path: Chromium's built-in HLS demuxer (`HlsManifestDemuxerEngine`) feeding encrypted samples to Starboard's `AVSampleBufferDisplayLayer` (AVSBDL) with FairPlay DRM. This is **not** about the AVPlayer/UrlPlayer path. The UrlPlayer (AVPlayer) path is referenced only for comparison. For UrlPlayer documentation, see `Phase-1.md`, `Phase-2-FairPlay-DRM-Design-Doc.md`, and `EncryptedEventForwarding-Architecture.md`.
>
> **Related documents:**
> - [HLS Demuxer Modifications](HLS-Demuxer-Modifications.md) -- Demuxer/parser changes for clear + encrypted HLS
> - [HLS Demuxer Design Doc](HLS-Demuxer-Design-Doc.md) -- Complete design doc for reviewer
> - [AVContentKeySession Investigation](AVContentKeySession-Investigation.md) -- Remaining blocker: manual FairPlay key request for AVSBDL path
> - [FairPlay Handshake Debug Log](FairPlay-Handshake-Debug-Log.md) -- Physical-device handshake runs, tested content identifiers, CKC shapes, and current errors

## 1. Executive Summary

This document provides a code-verified analysis of the feasibility of a Proof of Concept (POC) integrating Chromium's built-in HLS demuxer with Starboard's stream-based pipeline on tvOS, utilizing `AVSampleBufferDisplayLayer` (AVSBDL) and FairPlay DRM.

**Verified Finding:** The POC is technically viable and the architectural direction is sound. Routing demuxed streams through AVSBDL is the only path to support **VP9, AV1, and Opus** on tvOS, as the native `AVPlayer` is restricted to H.264, HEVC, and AAC/AC3/EAC3. While Starboard's tvOS media stack is mature and already supports manual FairPlay key attachment for both audio and video renderers, the primary engineering effort lies in modifying `HlsManifestDemuxerEngine` to handle FairPlay-specific logic. Estimated total effort: **5-9 engineering days**.

---

## 2. Proposed Architecture (Stream-Based POC)

The POC will route demuxed elementary streams through Starboard's stream-based `SbPlayer` interface. On tvOS, this pipeline runs within the main Cobalt process (no separate GPU process).

### 2.1 HLS Content Structure

Before looking at code, it helps to understand how HLS content is organized. An HLS stream is a hierarchy of increasingly smaller units:

```
Manifest (.m3u8)                         <-- Text file: list of URLs to segments
  |
  +-- Segment (.m4s, ~6 seconds each)    <-- A downloadable chunk of video+audio
       |
       +-- Container (fMP4 format)        <-- File format: organizes bytes into boxes
            |
            +-- Sample                    <-- One video frame or one audio packet
                 |
                 +-- Block (16 bytes)     <-- The unit AES encryption operates on
```

A 2-hour concert at 6-second segments = ~1200 segments. Each segment at 30fps contains ~180 video samples (frames) and ~260 audio samples. Each sample is divided into 16-byte blocks for encryption. With FairPlay's `cbcs` scheme, only 1 in every 10 blocks per sample is encrypted -- enough to make the frame unwatchable but light on CPU.

*Derived from: RFC 8216 (HLS spec), ISO 23001-7 (Common Encryption), Apple FairPlay Streaming Overview PDF.*

### 2.2 Component Pipeline

Each component in the pipeline operates on a specific level of this hierarchy:

```
[Cobalt Process]
  HTML <video src="something.m3u8">
    -> WebMediaPlayerImpl
      -> ManifestDemuxer
        -> HlsManifestDemuxerEngine          [works with: Manifest + Segments]
          -> [RenditionManager] -> ABR logic    (picks which quality variant)
          -> [SegmentFetcher]  -> Fetches .m4s  (downloads segment files)
          -> [ManifestDemuxer::AppendAndParseData]
            -> ChunkDemuxer                    [works with: Container + Samples]
              -> DemuxerStream                   (outputs individual frames/packets)

  StarboardRenderer (Mojo Client)
    -> SbPlayerBridge
      -> SbPlayerCreate (Stream-based)
        |
        +-- AVSBVideoRenderer (tvOS)           [works with: Samples + Blocks]
        |     -> AVVideoSampleBufferBuilder      (AnnexB -> AVCC/CMSampleBuffer)
        |     -> [AVSampleBufferAttachContentKey] (tags each sample with FairPlay key)
        |     -> AVSampleBufferDisplayLayer       (Apple hardware decrypts blocks, renders)
        |
        +-- AVSBAudioRenderer (tvOS)           [works with: Samples + Blocks]
              -> AVAudioSampleBufferBuilder
              -> [AVSampleBufferAttachContentKey] (tags each sample with FairPlay key)
              -> AVSampleBufferAudioRenderer       (Apple hardware decrypts blocks, renders)
```

---

## 3. Technical Analysis (Code-Verified)

### 3.0 What the POC Must Change (Plain English)

Today, when Chromium's HLS engine encounters a FairPlay-encrypted segment, it does not know what to do. The engine was built only for `AES-128` (where Chromium fetches the key itself and decrypts the whole segment in software). For FairPlay (`SAMPLE-AES`), the engine needs to do something fundamentally different: **not decrypt anything**. Instead, it must:

1. **Read the manifest label** -- see `METHOD=SAMPLE-AES` and `URI="skd://..."` in the `.m3u8` file. (The parser already does this.)
2. **Recognize the `skd://` scheme** -- instead of HTTP-fetching the key URI, understand that `skd://` means "ask JavaScript for the key via EME." (The engine does NOT do this today.)
3. **Pass encrypted samples through unchanged** -- let ChunkDemuxer parse the container and output encrypted frames. Do not attempt software decryption. (The engine errors out today.)
4. **Fire the `encrypted` event** -- tell the web page "I found encrypted content, here is the key ID." (ManifestDemuxer kills the pipeline today instead of forwarding.)
5. **Let Apple hardware decrypt** -- once JS completes the license exchange, each sample gets tagged with the FairPlay key at the renderer level. Apple's hardware decrypts the cbcs blocks. (This part already works.)

*Derived from: Chromium HLS Player Design Doc (confirms only AES-128 was implemented), code analysis of hls_manifest_demuxer_engine.cc and manifest_demuxer.cc.*

### 3.1 Chromium HLS Demuxer Gaps (Verified in `media/`)

| Component | Code Status | Required Action |
|-----------|-------------|-----------------|
| **`XKeyTag` Parser** | **Complete**: `RecognizeMethod` (tags.cc:1547) already recognizes `SAMPLE-AES`, `SAMPLE-AES-CTR`, and `SAMPLE-AES-CENC`. | None. The parser is already feature-complete for these tags. |
| **`HlsManifestDemuxerEngine`** | **Gap**: Actively returns `kUnsupportedCryptoMethod` (lines 996-998) for all SAMPLE-AES variants. Attempts HTTP fetch for all key URIs regardless of scheme. | Handle `kSampleAES` (FairPlay/cbcs) and `kSampleAESCTR` (cenc): bypass software decryption, detect `skd://` URIs, and forward to `ManifestDemuxer` as EME init data. |
| **`EncryptionData`** | **Complete**: Already stores the `uri_`, `method_`, and `format_` (media_segment.h:92). | None. Data structures are sufficient. |
| **EME Signaling** | **Gap**: `ManifestDemuxer::OnEncryptedMediaData` (manifest_demuxer.cc:645) hardcoded to `OnError`. | Fix: Forward to `host_->OnEncryptedMediaInitData(type, data)`. This one-line change enables the `encrypted` event to fire in JS. |

### 3.2 Starboard tvOS Capabilities (Verified in `starboard/tvos/shared/media/`)

- **Rendering Stack**: Both `AVSBVideoRenderer` and `AVSBAudioRenderer` call `AVSampleBufferAttachContentKey` once a valid `AVContentKey` is retrieved from the DRM system.
- **DRM System**: The architecture uses `DrmSystemPlatform` which delegates to `SBDApplicationDrmSystem`. The DRM-side plumbing for `skd://` (including SPC generation via `generateSessionUpdateRequestForSkd:`) is already complete and working.
- **CDM Constraints**: `StarboardRenderer::SetCdm` explicitly rejects CDM switching after initialization. The POC must ensure the CDM is attached early.

---

## 4. Blockers: What Must Be Resolved Before Anything Plays

Before addressing encryption-specific gaps (Section 3.1), there are infrastructure blockers that prevent **any** HLS content (clear or encrypted) from reaching the new demuxer. These must be resolved first.

### 4.1 Build and Feature Flags

| # | Blocker | File | Current State | Fix |
|---|---------|------|---------------|-----|
| **B0** | HLS demuxer not compiled | `cobalt/build/configs/common.gn` | `enable_hls_demuxer = false` | Set to `true`. Also requires `enable_mse_mpeg2ts_stream_parser = true` (asserted at `media_options.gni:170`). May introduce new build dependencies -- needs a test build. |
| **B1** | Feature flag disabled | `media/base/media_switches.cc:1168` | `kBuiltInHlsPlayer` = `FEATURE_DISABLED_BY_DEFAULT` for non-Android | Enable via Cobalt feature list override or build flag for tvOS. |

Without B0 and B1, all `#if BUILDFLAG(ENABLE_HLS_DEMUXER)` code is either absent or inactive. The `CreateHlsDemuxer()` path at `demuxer_manager.cc:556` does not exist.

### 4.2 URL Routing Conflict

| # | Blocker | File | Current State | Fix |
|---|---------|------|---------------|-----|
| **B2** | `UrlPlayerDemuxer` intercepts `.m3u8` | `demuxer_manager.cc:352-361` | Under `#elif BUILDFLAG(USE_STARBOARD_MEDIA)`, any URL containing `.m3u8` is routed to `UrlPlayerDemuxer` (AVPlayer). This runs in `CreateDemuxer()` before the HLS demuxer path. | Add a conditional: if a POC flag is active, skip `UrlPlayerDemuxer` and let `.m3u8` fall through to the `CreateHlsDemuxer()` path. Both paths must coexist -- AVPlayer for production, HLS demuxer for POC. |

### 4.3 Network / Data Source

| # | Blocker | File | Current State | Fix |
|---|---------|------|---------------|-----|
| **B3** | `HlsDataSourceProvider` availability | `demuxer_manager.cc:561` | `HlsManifestDemuxerEngine` requires an `HlsDataSourceProvider` from `client_->GetHlsDataSourceProvider()`. This originates in `WebMediaPlayerImpl` via `MultiBufferDataSourceFactory` in Blink. On Cobalt/Starboard, the data source infrastructure may differ. | Verify that `GetHlsDataSourceProvider()` returns a working provider on tvOS. If not, implement an adapter or use Cobalt's network stack. **Unknown complexity -- needs investigation.** |

### 4.4 Codec / Renderer Readiness

| # | Blocker | Affects | Current State | Fix |
|---|---------|---------|---------------|-----|
| **B4** | VP9/Opus sample building | Clear VP9/Opus only | `AVVideoSampleBufferBuilder` handles H.264 AnnexB-to-AVCC conversion. VP9 and Opus sample creation may not be wired. H.264/AAC should work for Phase 1 since AVSBDL already handles these from the DASH/MSE path. | For Phase 1: use H.264/AAC test content (no work needed). For VP9/Opus: extend builder (medium effort). |

### 4.5 Encryption-Specific Blockers (from Section 3.1)

| # | Blocker | File | Fix |
|---|---------|------|-----|
| **B5** | Engine errors on SAMPLE-AES variants | `hls_manifest_demuxer_engine.cc:996` | Handle `kSampleAES`/`kSampleAESCTR` in switch: bypass software decryption, detect `skd://`, forward as EME init data. |
| **B6** | `OnEncryptedMediaData` kills pipeline | `manifest_demuxer.cc:645` | Replace `OnError(PIPELINE_ERROR_INVALID_STATE)` with `host_->OnEncryptedMediaInitData(type, data)`. |

### 4.6 Summary: Blockers by Phase

```
Phase 1 (Clear fMP4 HLS):
  Must fix: B0, B1, B2, B3
  Might need: B4 (only if using VP9/Opus; H.264/AAC should work)

Phase 2 (FairPlay/cbcs):
  Must fix: B5, B6 (in addition to all Phase 1 blockers)

Phase 3 (DRM Loopback):
  No new blockers -- DRM and renderer code already works from AVPlayer path.
  This phase is verification, not new development.
```

*Derived from: Build config at cobalt/build/configs/common.gn, feature flags at media/base/media_switches.cc:1168, routing logic at demuxer_manager.cc:329-361, data source wiring at demuxer_manager.cc:561.*

---

## 5. Comparison of FairPlay Integration Models (Path A vs Path B)

| Feature | Path A: AVSBDL (Manual) | Path B: AVPlayer (Automated) |
|---------|-------------------------|------------------------------|
| **Codec Support** | **VP9, AV1, Opus**, HEVC, H.264 | HEVC, H.264, AAC, AC3, **EAC3** |
| **ABR & DAI** | Full control via `RenditionManager` | Black box (System controlled) |
| **DRM Mechanism** | `AVSampleBufferAttachContentKey` | `[AVContentKeySession addContentKeyRecipient:URLAsset]` |
| **Key Discovery** | **Manual**: `HlsManifestDemuxerEngine` must identify `skd://` and trigger EME. | **Automated**: `AVPlayer` natively parses the manifest and triggers key requests. |
| **Decryption** | **Per-Sample**: Every audio/video sample is manually tagged with a key. | **Asset-Level**: Decryption is handled transparently by the OS. |

---

## 6. POC Implementation Strategy

### Phase 1: Clear fMP4 HLS via Chromium Demuxer

*Goal: Prove the pipeline works end-to-end with unencrypted content. This validates that Chromium's HLS engine can fetch segments, ChunkDemuxer can parse fMP4, and AVSBDL can render the resulting samples.*

- Force `DemuxerManager` to use `HlsManifestDemuxerEngine` for `.m3u8` on tvOS.
- Verify plaintext fMP4 playback through `AVSBDL`.
- This avoids all encryption complexity. If a clear HLS stream plays, the manifest-to-segment-to-container-to-sample pipeline is validated.

### Phase 2: FairPlay/`cbcs` Logic in HLS Engine

*Goal: Teach the engine to recognize FairPlay encryption and signal it to JavaScript, instead of trying to decrypt or erroring out. This is the main engineering work.*

- Refactor `HlsManifestDemuxerEngine` to handle `METHOD=SAMPLE-AES` (`kSampleAES`), which is the FairPlay encryption method for fMP4 containers. Per the HLS spec (RFC 8216bis), `SAMPLE-AES` with fMP4 maps to the Common Encryption `cbcs` scheme (AES-CBC with 1:9 pattern). Note: `SAMPLE-AES-CTR` (`kSampleAESCTR`) maps to the `cenc` scheme (AES-CTR) and is used by Widevine/PlayReady over HLS, not FairPlay. *(Derived from: RFC 8216bis, Apple FairPlay Streaming Overview.)*
- Add a scheme check for `skd://` in `EncryptionData::uri_`. If found, bypass the HTTP key fetch and instead forward the raw URI as `init_data` via `ManifestDemuxer`. In plain terms: when the engine sees the manifest label says "key is at `skd://...`", it should tell JavaScript about it instead of trying to download it. *(Derived from: code analysis of hls_manifest_demuxer_engine.cc switch statement at lines 966-1000.)*
- Update `ManifestDemuxer::OnEncryptedMediaData` to forward the event to the pipeline host instead of calling `OnError`. This is the bridge between "engine found encrypted content" and "JavaScript receives the `encrypted` event." *(Derived from: code analysis of manifest_demuxer.cc:645-648.)*

### Phase 3: DRM Loopback

*Goal: Verify that the key exchange and per-sample decryption work when driven by the HLS engine (instead of AVPlayer). The DRM and renderer code is already proven to work from the AVPlayer/URL-player path. This phase confirms it also works when samples arrive from ChunkDemuxer via AVSBDL.*

- Verify the end-to-end flow using the existing `generateSessionUpdateRequestForSkd:` path in `SBDApplicationDrmSystem`. This method (commit `8278552`) already handles UTF-8 `skd://` URIs, looks up the pending `AVContentKeyRequest`, and generates the SPC. *(Derived from: application_drm_system.h:96, FairPlay-GenerateRequest-Gap-Analysis.md resolution status.)*
- Ensure that both audio and video renderers successfully attach the resulting `AVContentKey` to their respective samples. Each `CMSampleBuffer` must be tagged with the key before being enqueued to the display/audio layer. *(Derived from: av_sample_buffer_video_renderer.mm:505, av_sample_buffer_audio_renderer.mm:192.)*

---

## 7. Encryption Scheme Reference

### 7.1 The Four Layers

FairPlay encryption involves four distinct layers. They look similar (some even share the string `"cenc"`) but operate at completely different stages of the content lifecycle. Confusing them leads to incorrect implementation.

| Layer | Concept | FairPlay Value | Widevine Value |
|-------|---------|---------------|----------------|
| **HLS Manifest** (`EXT-X-KEY METHOD=`) | How segments are encrypted | `SAMPLE-AES` (per-sample AES-CBC pattern) | `SAMPLE-AES-CTR` (per-sample AES-CTR) |
| **Common Encryption** (ISO 23001-7) | Standardized crypto scheme for fMP4 | `cbcs` (AES-CBC, 1:9 pattern) | `cenc` (AES-CTR, full sample) |
| **EME `encryptionScheme`** | JS capability query value | `"cbcs"` | `"cenc"` |
| **EME `initDataType`** | Format of key identification data | `"skd"` (skd:// URI, UTF-8) | `"cenc"` (PSSH box, binary) |

**Key distinction**: `SAMPLE-AES` with fMP4 = `cbcs` (FairPlay). `SAMPLE-AES-CTR` with fMP4 = `cenc` (Widevine). The Apple FairPlay Streaming Overview confirms FairPlay uses AES-CBC mode for both video (per-frame) and audio (per-sample) encryption.

For MPEG-TS containers, `SAMPLE-AES` follows Apple's separate "HLS Sample Encryption" specification rather than Common Encryption. `SAMPLE-AES-CTR` is **not defined** for MPEG-TS.

### 7.2 Simple Explanation: A Movie Studio Example

Imagine you run a small movie studio. You recorded a concert film and want to deliver it securely to viewers on Apple TV via HLS with FairPlay DRM. Here is how each layer maps to a real production step.

**Layer 1: Common Encryption (cbcs) -- "How you lock each frame at the factory"**

*Production stage: Content packaging / encoding*

After you finish editing, your packaging tool (e.g., Shaka Packager, Bento4) encrypts the video file. It does not encrypt the entire file like a zip password. Instead, it goes frame by frame:

- For each video frame, it encrypts **1 out of every 10 blocks** using AES-CBC. The other 9 blocks stay in the clear. This is the "1:9 pattern" -- enough to make the frame unwatchable without the key, but light enough that the device's hardware can decrypt in real time.
- Audio samples are fully encrypted with AES-CBC.

This is the `cbcs` scheme from the Common Encryption standard (ISO 23001-7). It is the **actual math** that scrambles the bytes. Think of it as the type of lock you put on each frame.

Widevine uses a different lock: `cenc`, which uses AES-CTR (counter mode) and encrypts the entire sample. Same idea, different padlock.

```
Your raw video frame:  [clear header][encrypted block][clear][clear][clear]...
                        ^-- cbcs pattern: 1 encrypted per 9 clear
```

**Layer 2: HLS Manifest (SAMPLE-AES) -- "The label on the shipping box"**

*Production stage: HLS playlist generation*

Your packaging tool also generates the `.m3u8` playlist. It needs to tell the player "these segments are encrypted, and here is where to find the key." It writes:

```
#EXT-X-KEY:METHOD=SAMPLE-AES,URI="skd://my-content-id-123",KEYFORMAT="com.apple.streamingkeydelivery"
#EXTINF:6.0,
segment001.m4s
```

`METHOD=SAMPLE-AES` is the **label on the shipping box**. It tells the HLS player: "The frames inside are encrypted per-sample using AES-CBC pattern (cbcs)." It does not do any encryption itself -- it just describes what was already done in Layer 1.

If you were using Widevine instead, the label would say `METHOD=SAMPLE-AES-CTR`.

Note: There is also `METHOD=AES-128`, which is a completely different thing -- it encrypts the **entire segment file** (like putting the whole box in a safe). That is transport-level encryption, not DRM. Chromium already handles AES-128 in software. Our POC is about the per-sample DRM path.

**Layer 3: EME encryptionScheme ("cbcs") -- "The buyer asks: do you support this lock type?"**

*Production stage: None -- this happens at playback time in the browser*

Before the video plays, the web page's JavaScript asks the browser: "Can you handle FairPlay DRM with cbcs encryption?" This is the `requestMediaKeySystemAccess()` call:

```js
navigator.requestMediaKeySystemAccess("com.youtube.fairplay", [{
    initDataTypes: ["skd"],
    videoCapabilities: [{
        contentType: 'video/mp4; codecs="avc1.42E01E"',
        encryptionScheme: "cbcs"    // <-- Layer 3: "can you open cbcs locks?"
    }]
}]);
```

The browser checks: "Do I have a CDM (Content Decryption Module) that supports cbcs?" For FairPlay on tvOS, the answer is yes -- Apple's hardware handles cbcs natively.

This is a **capability check**, not encryption. No bytes are scrambled here. It is the buyer calling the locksmith and asking "do you have a key cutter for this type of lock?"

If you omit `encryptionScheme`, Chromium defaults to `"cenc"` -- and FairPlay only supports `"cbcs"`, so the check silently fails. This is a known gotcha (see `EME-FairPlay-ConfigSelector-Analysis.md`).

**Layer 4: EME initDataType ("skd") -- "The shipping label that says where to pick up the key"**

*Production stage: None -- this also happens at playback time*

When the player encounters the encrypted content, it fires an `encrypted` event to JavaScript. The event carries two pieces of information:

- `initDataType: "skd"` -- this tells JS the format of the key identifier. For FairPlay, it is an `skd://` URI.
- `initData: "skd://my-content-id-123"` -- the actual key identifier, as raw UTF-8 bytes.

```js
video.addEventListener('encrypted', (event) => {
    // event.initDataType = "skd"           <-- Layer 4: format of the key ID
    // event.initData = "skd://my-content-id-123"  <-- the actual key ID
    session.generateRequest(event.initDataType, event.initData);
});
```

JS uses this to ask the license server for the decryption key. The license server looks up `my-content-id-123`, finds the matching AES key, wraps it securely (SPC/CKC exchange), and sends it back.

For Widevine, the `initDataType` is `"cenc"` and the `initData` is a PSSH box (a binary blob containing key IDs). Same concept, different envelope format.

**The confusing part**: the string `"cenc"` appears in both Layer 2 (Common Encryption scheme name), Layer 3 (EME encryptionScheme), and Layer 4 (EME initDataType for Widevine). They mean three different things:

| Where you see `"cenc"` | What it means |
|------------------------|---------------|
| Common Encryption scheme | AES-CTR crypto mode (the math) |
| EME `encryptionScheme: "cenc"` | "I need a player that can do AES-CTR" (capability check) |
| EME `initDataType: "cenc"` | "The key ID is in a PSSH box" (envelope format) |

### 7.3 What the POC Touches at Each Level

Each component in the pipeline operates on a specific level of the content hierarchy, and each encryption layer is relevant at a specific point:

| Content Level | Component | Encryption Layer Active | POC Status |
|--------------|-----------|------------------------|------------|
| **Manifest** (.m3u8) | `HlsManifestDemuxerEngine` | Layer 2: Reads `METHOD=SAMPLE-AES`, `URI="skd://..."` | **Needs work**: must recognize SAMPLE-AES and `skd://` scheme |
| **Segment** (.m4s) | `HlsManifestDemuxerEngine` + `HlsDataSourceProvider` | None (segments are fetched as-is, still encrypted) | Works (fetching is the same for clear or encrypted) |
| **Container** (fMP4 boxes) | `ChunkDemuxer` | Layer 1 metadata: reads `tenc`/`senc` boxes describing cbcs encryption | Should work (ChunkDemuxer already parses encrypted fMP4 for DASH/MSE) |
| **Sample** (video frame / audio packet) | `AVSBVideoRenderer` / `AVSBAudioRenderer` | Layer 1 applied: each sample's blocks are cbcs-encrypted | **Works**: `AVSampleBufferAttachContentKey` already tags samples |
| **Block** (16 bytes) | Apple hardware | Layer 1 reversed: hardware decrypts 1-in-10 blocks | Works (OS handles this transparently once key is attached) |
| **JS/EME** | `ManifestDemuxer` -> `WebMediaPlayerImpl` -> JS | Layer 3 (capability check) + Layer 4 (init data forwarding) | **Needs work**: `OnEncryptedMediaData` must forward, not error |

*Derived from: Code analysis of the full pipeline (hls_manifest_demuxer_engine.cc, manifest_demuxer.cc, av_sample_buffer_video_renderer.mm), RFC 8216bis, ISO 23001-7.*

### 7.4 End-to-End Production Flow

```
YOU (Content Creator)
  |
  v
[1. Record concert film] --> raw video (concert.mov)
  |
  v
[2. Package with encryption]
  Tool: Shaka Packager / Bento4 / Apple's HLS tools
  Input:  concert.mov + AES-128 content key + content ID
  Output: encrypted fMP4 segments (.m4s) + .m3u8 playlist
  |
  |  What happens inside the tool:
  |    Layer 1 (Common Encryption): Each frame encrypted with cbcs
  |    Layer 2 (HLS Manifest):      Playlist tagged with METHOD=SAMPLE-AES, URI=skd://content-id
  |
  v
[3. Upload to CDN] --> segments + playlist on content server
                       content key stored on license server (KSM)
  |
  v
VIEWER (Apple TV running Cobalt)
  |
  v
[4. Web page loads, JS checks capability]
  Layer 3 (EME encryptionScheme): "Can you do cbcs?" --> Yes
  |
  v
[5. Player fetches .m3u8, finds EXT-X-KEY with skd://]
  Layer 2 (HLS Manifest): Player reads the label
  |
  v
[6. Player fires 'encrypted' event to JS]
  Layer 4 (EME initDataType): type="skd", data="skd://content-id"
  |
  v
[7. JS talks to license server, gets content key]
  SPC/CKC exchange via FairPlay protocol
  |
  v
[8. Key delivered to hardware, frames decrypted]
  Layer 1 (Common Encryption): Hardware reverses the cbcs math
  |
  v
[9. Concert plays on screen]
```

### 7.5 References

| Source | What It Covers |
|--------|---------------|
| [RFC 8216](https://datatracker.ietf.org/doc/html/rfc8216) -- HTTP Live Streaming | HLS `EXT-X-KEY` METHOD values: `AES-128`, `SAMPLE-AES` |
| [RFC 8216bis (draft)](https://datatracker.ietf.org/doc/html/draft-pantos-hls-rfc8216bis) -- HLS 2nd Edition | Adds `SAMPLE-AES-CTR`, `AES-256-GCM`; clarifies SAMPLE-AES = cbcs for fMP4 |
| [ISO 23001-7](https://www.iso.org/standard/68042.html) -- Common Encryption (CENC) | Defines `cenc`, `cens`, `cbc1`, `cbcs` schemes |
| [W3C EME Spec](https://www.w3.org/TR/encrypted-media/) -- Encrypted Media Extensions | Defines `encryptionScheme`, `initDataType`, `MediaKeySystemAccess` |
| [Apple FairPlay Streaming Overview](https://developer.apple.com/streaming/fps/FairPlayStreamingOverview.pdf) | Confirms FairPlay uses AES-CBC per-frame (video) and per-sample (audio) |
| [Apple HLS Sample Encryption Spec](https://developer.apple.com/library/archive/documentation/AudioVideo/Conceptual/HLS_Sample_Encryption/) | SAMPLE-AES details for MPEG-TS containers (H.264, AAC, AC-3) |
| [Apple FPS Developer Page](https://developer.apple.com/streaming/fps/) | SDK downloads, confirms CBCS support in SDK 26 |
| [WebKit CDMFairPlayStreaming.cpp](https://github.com/nicebyte/AnotherWebKitFork/blob/main/AnotherWebKitFork/AnotherWebKitFork/AnotherWebKitFork/Source/WebCore/platform/graphics/avfoundation/CDMFairPlayStreaming.cpp) | WebKit's supported initDataTypes: sinf, skd, cenc, mpts |
| Chromium `media/formats/hls/tags.h:448-479` | Enum: `kSampleAES`, `kSampleAESCTR`, `kSampleAESCENC` |
| Chromium `media/filters/hls_manifest_demuxer_engine.cc:966-1000` | The switch statement where SAMPLE-AES variants hit `kUnsupportedCryptoMethod` |
| Chromium [Native HLS Player Design Doc](HLS-Player-Design-Doc.pdf) | tmathmeyer/cassew: confirms only AES-128 was implemented; DRM deferred |

---

## 8. Complexity Assessment

### 8.1 Test Stream

The reference test stream is `fairplay-urlplayer-test.html` using the Axinom FairPlay CBCS test vector:
- **URL**: `https://media.axprod.net/TestVectors/Cmaf/protected_1080p_h264_cbcs/manifest.m3u8`
- **Container**: CMAF (fMP4 segments) delivered via HLS
- **Encryption**: cbcs (METHOD=SAMPLE-AES in manifest, Common Encryption AES-CBC 1:9 pattern)
- **Video**: H.264 High Profile (288p to 1080p @ 24fps)
- **Audio**: AAC-LC stereo
- **DRM**: FairPlay via `skd://` key delivery, Axinom license server

> Note: This test page was built for the UrlPlayer/AVPlayer path and already plays on device. The assessment below is for playing the **same stream** through the Chromium HLS demuxer + AVSBDL path, without AVPlayer.

### 8.2 Work Items

| # | Work Item | What Changes | Estimate | Risk |
|---|-----------|-------------|----------|------|
| **W1** | Enable HLS demuxer in build | Set `enable_hls_demuxer = true` in `cobalt/build/configs/common.gn`. Add feature flag override for `kBuiltInHlsPlayer`. Fix any compilation errors from newly included HLS code. | 0.5-1 day | Low |
| **W2** | Route .m3u8 to HLS demuxer | In `demuxer_manager.cc:352`, add a flag check: if POC mode, skip `UrlPlayerDemuxer` and fall through to `CreateHlsDemuxer()`. | 0.5 day | Low |
| **W3** | Verify HlsDataSourceProvider | `GetHlsDataSourceProvider()` at `web_media_player_impl.cc:1759` creates `HlsDataSourceProviderImpl` backed by `MultiBufferDataSourceFactory`. This uses standard Blink networking (not Starboard-specific). Should work if Cobalt's Blink networking works for page loads. Verify with a clear HLS stream first. | 0.5-1 day | Medium |
| **W4** | Handle SAMPLE-AES in engine + skd:// extraction | Two switch statements in `hls_manifest_demuxer_engine.cc` (lines 928 and 966) need new cases for `kSampleAES`. Must: (a) not software-decrypt, (b) detect `skd://` scheme in `EncryptionData::uri_`, (c) forward the raw `skd://` URI as `EmeInitDataType::SKD` init data to `ManifestDemuxer`. ~50-100 lines of new code. | 2-3 days | **High** |
| **W5** | Fix OnEncryptedMediaData + init data mapping | Replace `OnError(PIPELINE_ERROR_INVALID_STATE)` at `manifest_demuxer.cc:645` with forwarding to `host_->OnEncryptedMediaInitData(SKD, skd_uri_bytes)`. Additionally, ensure the HLS engine's manifest-level `skd://` signal reaches JS as the `encrypted` event, since ChunkDemuxer's container-level signaling may not carry the FairPlay key ID. | 1-2 days | **High** |
| **W6** | Integration testing | Test with Axinom stream end-to-end. Verify: manifest parsed, segments fetched, encrypted event fires, EME key exchange completes, samples decrypted by AVSBDL. | 1-2 days | Medium |
| **W7** | Test page changes | None needed. The existing `"skd"` branch in `fairplay-urlplayer-test.html` (line 341) passes init data through as-is to `generateRequest("skd", rawUri)`, which is handled by `generateSessionUpdateRequestForSkd:`. | 0 days | None |

**Total estimate: 5-9 engineering days**

### 8.3 The Hard Part: Manifest-to-EME Bridge (W4 + W5)

This is the architecturally novel piece that does not exist in upstream Chromium.

Chromium's HLS demuxer was designed for `AES-128`, where the engine handles encryption end-to-end (fetches key via HTTP, decrypts in software). For DRM (`SAMPLE-AES` with `skd://`), the engine must do something fundamentally different:

1. **Not decrypt** -- pass encrypted bytes through to ChunkDemuxer unchanged
2. **Extract key info from the manifest** -- the `skd://` URI comes from `EXT-X-KEY`, not from the fMP4 container
3. **Bridge two signaling levels** -- HLS manifest says "key at skd://...", but Chromium's EME expects encrypted events from the demuxer at the container level

The challenge is that ChunkDemuxer (which parses the fMP4) may fire its own `OnEncryptedMediaData` from `tenc`/`senc`/`pssh` boxes, but this would be `EmeInitDataType::CENC` (PSSH format), not the `skd://` URI that FairPlay needs. The HLS engine must extract the `skd://` URI from the manifest level and signal it as `EmeInitDataType::SKD` -- a new data path that upstream Chromium has never needed.

This is approximately 50-100 lines of code, but getting the lifecycle right (when to signal, what format, threading) requires understanding both the HLS engine's action queue and ChunkDemuxer's encryption detection.

### 8.4 What Already Works (No Changes Needed)

The vast majority of the pipeline is already built and tested from the UrlPlayer/AVPlayer path and the existing DASH/MSE path:

| Component | Why It Works | Verified By |
|-----------|-------------|-------------|
| `FairplayKeySystemInfo` | Registered, supports SKD/SINF/FAIRPLAY initDataTypes, cbcs scheme | EME-FairPlay-ConfigSelector-Analysis.md |
| `requestMediaKeySystemAccess` | Works for `com.youtube.fairplay` with `encryptionScheme: "cbcs"` | Device logs 2026-05-18 |
| `setServerCertificate` | Stores cert on `SBDApplicationDrmSystem` | Commit `be90623` |
| `generateSessionUpdateRequestForSkd:` | Handles raw UTF-8 skd:// URIs, generates SPC | Commit `8278552` |
| `session.update(CKC)` | Applies raw binary CKC to AVContentKeyRequest | Commit `71dbe3d` |
| `StarboardRenderer` stream-based encrypted path | Detects encrypted streams, waits for CDM, creates `SbPlayerCreate` | Existing DASH/MSE path |
| `AVSBVideoRenderer` + `AVSBAudioRenderer` | Call `AVSampleBufferAttachContentKey` per sample | `av_sample_buffer_video_renderer.mm:505`, `av_sample_buffer_audio_renderer.mm:192` |
| `ChunkDemuxer` fMP4 cbcs parsing | Handles cbcs via `tenc`/`senc` boxes | Existing DASH/MSE path |
| `HlsDataSourceProvider` (Blink networking) | Standard Blink `MultiBufferDataSourceFactory`, not Starboard-specific | `web_media_player_impl.cc:1759` |
| Test page JS | `"skd"` branch passes through as-is to `generateRequest` | `fairplay-urlplayer-test.html:341` |

### 8.5 Risk Summary

| Risk | Impact | Mitigation |
|------|--------|------------|
| `enable_hls_demuxer = true` pulls in code that does not compile on Cobalt | Blocks everything | Start with W1 as first step; fix build errors early |
| `HlsDataSourceProvider` does not work with Cobalt's network config | Blocks manifest/segment fetching | Test with clear HLS stream in Phase 1 before tackling encryption |
| ChunkDemuxer's container-level encryption signaling conflicts with manifest-level signaling | Double `encrypted` events or wrong init data type | Needs careful W4/W5 design to suppress or merge signals |
| `skd://` URI not recognized by downstream EME code | Key exchange fails silently | Already handled: `EmeInitDataType::SKD` enum and `ConvertToInitDataType("skd")` mapping exist (Plan-FairPlay-InitDataType-Support.md) |

---

## 9. Phase 1 Implementation Results: Clear HLS Playback (May 21-22, 2026)

**Status: WORKING.** Clear MPEG-TS HLS playback through Chromium HLS demuxer + AVSBDL confirmed on physical Apple TV 4K device.

Test stream: `https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8` (H.264 High Profile 1080p + AAC-LC, MPEG-TS segments, no encryption).

### 9.1 Bugs Found and Fixed

| Bug | Root Cause | Fix | Files |
|-----|-----------|-----|-------|
| `PIPELINE_ERROR_DECODE: BlockBufferCreate failed` | Starboard's `AvcParameterSets::CreateFromAnnexB` only recognized 4-byte AnnexB start codes (`00 00 00 01`). MPEG-TS streams use 3-byte (`00 00 01`) for slice NALUs. The scanner couldn't separate parameter sets from IDR data. | Added `GetAnnexBStartCodeSize()` that detects both 3 and 4-byte start codes. Updated `AdvanceToNextAnnexBHeader`, `CreateFromAnnexB`, `ConvertAnnexBToAvcc`, `GetAllSpses/Ppses`, `ConvertTo` to use variable start code sizes. | `starboard/shared/starboard/media/avc_util.cc` |
| `Failed to convert input data into avcc format` | `ConvertAnnexBToAvcc` rejected data starting with 3-byte start code. Also, AVCC output is larger than AnnexB input when 3-byte start codes expand to 4-byte length fields. | Added `GetAvccSizeFromAnnexB()` to pre-calculate output size. Caller allocates correct buffer size. Converter handles variable start codes per NALU. | `avc_util.cc`, `avc_util.h`, `avc_av_video_sample_buffer_builder.mm` |
| NALU type misidentification | `CreateFromAnnexB` compared full NALU header byte (`0x65`) instead of masking type bits (`byte & 0x1F == 5`). Missed IDR slices with `nal_ref_idc != 3`. | Use `kNaluTypeMask = 0x1F` for all NALU type checks. | `avc_util.cc` |
| SPS-only AUs emitted as frames | Cobalt's `es_parser_h264.cc` was behind upstream -- always called `EmitFrame()` even for access units with only SPS/PPS and no slice NALU. | Added `has_slice_nalu` guard (aligned with upstream Chromium). Only emit frame when IDR or non-IDR slice is found. | `media/formats/mp2t/es_parser_h264.cc` |
| Empty MIME type at SbPlayer | `ManifestDemuxerStream` did not override `mime_type()`, falling back to base class which returns `""`. Also, `AddAutoDetectedCodecsId` (HLS path) did not populate `id_to_mime_map_`. | Added `mime_type()` override forwarding to wrapped `ChunkDemuxerStream`. Populated `id_to_mime_map_` in `AddAutoDetectedCodecsId`. | `manifest_demuxer.h/.cc`, `chunk_demuxer.cc` |
| Audio stream gets `video/mp2t` MIME | Both audio and video streams from a single TS source got `video/mp2t`. | Refine MIME based on stream type: `video/mp2t` becomes `audio/mp2t` for audio streams. | `chunk_demuxer.cc` |
| Black screen (autoplay not firing) | `CanPlayThrough()` had no case for `kManifestDemuxer`. ReadyState stayed at `kHaveFutureData` (3), never reaching `kHaveEnoughData` (4) which is required for autoplay via `RequestAutoplayByAttribute`. Upstream Chromium already handles this. | Added `kManifestDemuxer` to `CanPlayThrough()` switch (aligned with upstream). | `web_media_player_impl.cc` |
| `SB_HAS_PLAYER_WITH_URL` blocking stream path | URL player was enabled, intercepting HLS before the HLS demuxer could run. | Set `SB_HAS_PLAYER_WITH_URL 0` in tvOS config. Unguarded FairPlay init data types in `starboard_cdm.cc`. | `configuration_public.h`, `starboard_cdm.cc` |
| Thread safety CHECK in codec queries | `CHECK(IsMainThread)` in `IsDecoderSupportedAudioType/VideoType` crashed because HLS demuxer's RenditionManager queries from media thread. | Removed CHECK (the query is read-only and thread-safe). Aligned with upstream `RenderMediaClient` behavior. | `cobalt_content_renderer_client.cc` |

### 9.2 Key Architectural Findings

1. **Upstream Chromium's decoder selection does not use `mime_type()` from DemuxerStream.** Decoders are selected based on `AudioDecoderConfig`/`VideoDecoderConfig` which come from SPS NALU parsing. The `mime_type()` on `DemuxerStream` is a Cobalt/Starboard-specific addition for `SbPlayer`, not used upstream.

2. **The Starboard `avc_util.cc` was designed for fMP4/AVCC streams** (which always use 4-byte start codes after BitstreamConverter processing). MPEG-TS streams arrive in raw AnnexB with mixed 3/4-byte start codes. This required fundamental changes to the start code scanning and AVCC conversion logic.

3. **The HLS demuxer path bypasses `EnableBitstreamConverter`** for ChunkDemuxer streams. Unlike the FFmpeg/MSE path, TS data arrives as AnnexB directly from the ES parser without any conversion step. The AVSBDL builder handles AnnexB-to-AVCC conversion.

4. **Cobalt's `es_parser_h264.cc` was behind upstream** in several ways (missing `has_slice_nalu`, missing `base::span` APIs). The `has_slice_nalu` fix was essential for correctness with TS streams.

### 9.3 Verified Pipeline Flow (Clear HLS)

```
HTML <video src="x36xhzz.m3u8" autoplay muted>
  -> WebMediaPlayerImpl::DoLoad (HLS URL detected, skip data source)
    -> DemuxerManager::CreateDemuxer (kBuiltInHlsPlayer=1, ManifestDemuxer)
      -> HlsManifestDemuxerEngine (fetches .m3u8, picks variant)
        -> ChunkDemuxer::AddAutoDetectedCodecsId("primary", kMP2T)
          -> Mp2tStreamParser (parses TS segments)
            -> EsParserH264 (extracts H.264 NALUs, AnnexB format)
            -> EsParserAdts (extracts AAC ADTS frames)
              -> ChunkDemuxerStream (AUDIO: audio/mp2t, VIDEO: video/mp2t)
                -> ManifestDemuxerStream (forwards mime_type)
                  -> MojoDemuxerStreamImpl (sends mime over Mojo IPC)
                    -> MojoDemuxerStreamAdapter (GPU process)
                      -> StarboardRenderer::CreatePlayerBridge
                        -> SbPlayerBridge (codec from VideoDecoderConfig)
                          -> AvcAVVideoSampleBufferBuilder
                            - CreateFromAnnexB (3/4-byte start codes)
                            - ConvertAnnexBToAvcc (variable start code sizes)
                            - CMSampleBufferCreateReady
                              -> AVSampleBufferDisplayLayer (hardware decode)
                          -> AacAVSampleBufferBuilder
                            - ADTS frames -> CMSampleBuffer
                              -> AVSampleBufferAudioRenderer
```

### 9.4 What Remains for Phase 2 (Encrypted HLS / FairPlay)

With clear HLS working, the remaining work for encrypted HLS is:
- W4: `skd://` URI recognition in `HlsManifestDemuxerEngine`
- W5: EME signaling bridge (manifest-level `EXT-X-KEY` to `encrypted` event)
- W6: FairPlay key attachment to AVSBDL samples
- Testing with FairPlay-encrypted HLS streams

---

## 10. Conclusion

The Phase 1 POC (clear HLS) is **complete and verified** on physical hardware. The Phase 2 work (encrypted HLS with FairPlay) can now proceed with confidence that the demuxer-to-decoder pipeline is functional.

The POC is the only viable path to provide YouTube's full codec suite (VP9/Opus) on tvOS while maintaining FairPlay DRM requirements. The engineering focus should be on the **HLS Engine's handling of `skd://` URIs** and the **EME signaling path** (W4 + W5), as the Starboard rendering and DRM components are already mature and capable of supporting this architecture.

Estimated total effort is **5-9 engineering days**, with the primary risk being the manifest-to-EME bridge logic (W4 + W5) which is architecturally novel -- upstream Chromium has never had a case where DRM key information is signaled from the HLS manifest level rather than the fMP4 container level.
