# Analysis: Chromium HLS Demuxer + AVSBDL + FairPlay POC for tvOS

*Author: Cobalt Engineer*
*May 2026*

## 1. Executive Summary

This document provides a code-verified analysis of the feasibility of a Proof of Concept (POC) integrating Chromium's built-in HLS demuxer with Starboard's stream-based pipeline on tvOS, utilizing `AVSampleBufferDisplayLayer` (AVSBDL) and FairPlay DRM.

**Verified Finding:** The POC is technically viable but requires non-trivial extensions to Chromium's HLS parser and engine. While Starboard's tvOS stack is mature and already supports manual FairPlay key attachment for AVSBDL, Chromium's HLS infrastructure currently lacks support for `SAMPLE-AES`, `cbcs` (fMP4), and the `skd://` URI scheme required for FairPlay.

---

## 2. Proposed Architecture (The "Path A" POC)

The POC will route demuxed elementary streams through Starboard's stream-based `SbPlayer` interface, specifically targeting the `AVSBDL` renderer.

```
[Renderer Process]
  HTML <video src="something.m3u8">
    -> WebMediaPlayerImpl
      -> ManifestDemuxer
        -> HlsManifestDemuxerEngine
          -> [EXT-X-KEY Parser] -> Extracts skd:// URI (Currently missing)
          -> [RenditionManager] -> ABR logic
          -> [SegmentFetcher]  -> Fetches segments
          -> [ManifestDemuxer::AppendAndParseData]
            -> ChunkDemuxer (Parses container)
              -> DemuxerStream (Encrypted H.264/AAC)

[GPU Process]
  StarboardRenderer (Mojo Client)
    -> StarboardPlayerBridge
      -> SbPlayerCreate (Stream-based)
        -> AVSBVideoRenderer (tvOS specific)
          -> AVVideoSampleBufferBuilder (AnnexB -> AVCC/CMSampleBuffer)
          -> [AVSampleBufferAttachContentKey] (FairPlay Decryption - VERIFIED)
          -> AVSampleBufferDisplayLayer (Rendering)
```

---

## 3. Technical Analysis (Code-Verified)

### 3.1 Chromium HLS Demuxer Gaps (Verified in `media/formats/hls/`)

| Component | Code Status | Required Action |
|-----------|-------------|-----------------|
| **`XKeyTag` Parser** | Supports `AES-128`, `AES-256`, and `NONE`. `RecognizeMethod` stubs exist for `SAMPLE-AES`. | Implement full `SAMPLE-AES` recognition and allow the `skd://` URI scheme in the `URI` attribute. |
| **Encryption Logic** | `HlsManifestDemuxerEngine` performs **software decryption** for AES-128 using `crypto::aes_cbc::Decrypt`. | Disable software decryption for `SAMPLE-AES`/`cbcs`. Ensure the engine passes encrypted blocks to `ChunkDemuxer` with appropriate metadata. |
| **`EncryptionData`** | Only stores `key_` (raw bytes) and `iv_`. | Extend to support `init_data` (the `skd://` URI) for transfer to the CDM. |
| **EME Signaling** | `ManifestDemuxer::OnEncryptedMediaData` is a hardcoded error (`OnError(PIPELINE_ERROR_INVALID_STATE)`). | Implement this method to fire the `encrypted` event to JavaScript with the `skd://` init data. |

### 3.2 Starboard tvOS Capabilities (Verified in `starboard/tvos/shared/media/`)

- **`AVSBVideoRenderer`**: Explicitly calls `AVSampleBufferAttachContentKey` in `OnSampleBufferBuilderOutput`. It retrieves the `AVContentKey` via `drm_system_->GetContentKey()` using the sample's `drm_info->identifier`.
- **`DrmSystemFairplay`**: Implements `SbDrmSystem` using `AVContentKeySession`. It is a registered `AVContentKeyRecipient` and handles the SPC/CKC flow, caching `AVContentKey` objects for the renderer.
- **`StarboardRenderer`**: Correctly handles the transition from `Initialize()` to `SetCdm`, ensuring the DRM system is connected before playback starts.

---

## 4. Comparison of FairPlay Integration Models

A critical distinction exists in how FairPlay is integrated between the two paths:

| Feature | Path A: AVSBDL (Manual) | Path B: AVPlayer (Automated) |
|---------|-------------------------|------------------------------|
| **API Mechanism** | `AVSampleBufferAttachContentKey` | `[AVContentKeySession addContentKeyRecipient:URLAsset]` |
| **Key Discovery** | **Manual**: Chromium HLS parser must find `#EXT-X-KEY`, extract `skd://`, and trigger EME. | **Automated**: `AVPlayer` natively parses the manifest and triggers `didProvideContentKeyRequest` automatically. |
| **Decryption** | **Per-Sample**: Every `CMSampleBuffer` is manually tagged with a key before enqueuing to AVSBDL. | **Asset-Level**: Decryption is handled transparently by the OS for the entire asset unit. |
| **Synchronization** | Complex: Must ensure CDM has the key before the renderer requests it for a specific PTS. | Simple: Handled internally by `AVFoundation`. |

---

## 5. POC Implementation Strategy

### Phase 1: Clear fMP4 HLS via Chromium Demuxer
- Force `DemuxerManager` to use `HlsManifestDemuxerEngine` for `.m3u8` on tvOS.
- Verify plaintext fMP4 playback through `AVSBDL`. This avoids the complexities of AnnexB/AVCC conversion required for MPEG-TS.

### Phase 2: FairPlay Init Data & `cbcs` Support
- Extend `hls::XKeyTag::RecognizeMethod` to support `SAMPLE-AES`.
- Modify `HlsManifestDemuxerEngine` to extract the `skd://` URI and signal encryption via `ManifestDemuxer`.
- Update `ChunkDemuxer` to handle `cbcs` scheme signaling for HLS renditions.

### Phase 3: DRM Loopback
- Ensure `DrmSystemFairplay` receives the `skd://` URI (as `init_data`) and successfully resolves the `AVContentKey`.
- Verify the renderer successfully attaches this key to samples.

---

## 6. Conclusion

The POC is the correct path for achieving **VP9/opus/HDR** support for HLS on tvOS, as `AVPlayer` remains restricted to H.264/AAC. The primary engineering effort lies in **teaching Chromium's HLS demuxer about FairPlay (`skd://`)**, as the Starboard-to-tvOS bridge for encrypted stream-based playback is already functionally complete.
