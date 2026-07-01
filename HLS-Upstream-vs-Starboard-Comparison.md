# HLS Decoding: Upstream Chromium vs Cobalt Starboard (tvOS)

Both paths use the same conceptual Chromium HLS demuxer architecture: `HlsManifestDemuxerEngine` →
`ChunkDemuxer` → `DecoderBuffer` in AnnexB (video) or ADTS (audio; raw for xHE-AAC).
The files are not identical between chromium_main and cobalt-upstream — there are meaningful
differences in track selection, filtering, tracing/stats, and container sniffing — but the
architecture is shared. They diverge at the decode stage.

---

## The Two Pipelines Side by Side

```
                    ┌──────────────────────────────┐
                    │  HlsManifestDemuxerEngine    │
                    │  (same architecture, not identical files)     │
                    │                              │
                    │  Downloads .ts/.m4s segments  │
                    │  Feeds to ChunkDemuxer        │
                    └──────────────┬───────────────┘
                                   │
                    ┌──────────────┴───────────────┐
                    │  ChunkDemuxer + StreamParser  │
                    │  (same architecture, not identical files)     │
                    │                              │
                    │  TS → EsParserH264 (AnnexB)  │
                    │  TS → EsParserAdts (ADTS)    │
                    │  fMP4 → AnnexB 4-byte + ADTS │
                    └──────────────┬───────────────┘
                                   │
                          DecoderBuffer
                      (AnnexB video, ADTS audio;
                       raw AAC for xHE-AAC)
                                   │
              ┌────────────────────┴────────────────────┐
              │                                         │
    UPSTREAM CHROMIUM                         COBALT STARBOARD (tvOS)
              │                                         │
              ▼                                         ▼
    ┌─────────────────────┐               ┌──────────────────────────┐
    │  VIDEO              │               │  VIDEO                   │
    │                     │               │                          │
    │  H264Decoder        │               │  SbPlayerBridge          │
    │    ↓                │               │    ↓                     │
    │  Parses AnnexB      │               │  AvcAVVideoSample-      │
    │  NALU by NALU       │               │  BufferBuilder           │
    │  using H264Parser   │               │    ↓                     │
    │    ↓                │               │  ConvertAnnexBToAvcc()   │
    │  VideoToolbox-      │               │  (single-pass, handles   │
    │  H264Accelerator    │               │   3-byte & 4-byte SC)    │
    │    ↓                │               │    ↓                     │
    │  Reassembles AVCC   │               │  CMSampleBuffer          │
    │  per-NALU with      │               │    ↓                     │
    │  4-byte lengths     │               │  VideoToolbox HW decode  │
    │    ↓                │               │                          │
    │  CMSampleBuffer     │               └──────────────────────────┘
    │    ↓                │
    │  VideoToolbox       │
    │  HW decode          │
    └─────────────────────┘

    ┌─────────────────────┐               ┌──────────────────────────┐
    │  AUDIO              │               │  AUDIO                   │
    │                     │               │                          │
    │  FFmpegAudioDecoder │               │  SbPlayerBridge          │
    │  (normal AAC) or    │               │    ↓                     │
    │  AudioToolbox       │               │  AacAVSampleBuffer-      │
    │  (xHE-AAC/AC3/EAC3)│               │  Builder                 │
    │    ↓                │               │    ↓                     │
    │  FFmpeg handles     │               │  Detects ADTS sync word  │
    │  ADTS natively;     │               │  Strips 7-byte header    │
    │  AudioToolbox       │               │  (skips 0 for raw xHE)   │
    │  handles xHE-AAC   │
    │  raw samples using  │
    │  codec config       │               │    ↓                     │
    │    ↓                │               │  CMSampleBuffer with     │
    │  Decoded PCM audio  │               │  raw AAC payload         │
    │                     │               │    ↓                     │
    │                     │               │  CoreAudio HW decode     │
    └─────────────────────┘               └──────────────────────────┘
```

---

## Video: How Each Path Handles AnnexB → AVCC

Both paths must convert AnnexB to AVCC because Apple's VideoToolbox requires AVCC.
They do it differently:

### Upstream Chromium: Parse-then-reassemble

```
Location: media/gpu/mac/video_toolbox_h264_accelerator.cc

Step 1: H264Decoder parses the AnnexB DecoderBuffer
        using H264Parser::AdvanceToNextNALU()
        This handles both 3-byte and 4-byte start codes
        (h264_parser.cc:367-401)

Step 2: For each NALU, H264Decoder calls the accelerator:
        - SPS/PPS → SubmitFrameMetadata() → stores for format description
        - Slice → SubmitSlice() → accumulates raw NALU bytes (no start codes)

Step 3: SubmitDecode() assembles the final CMBlockBuffer:
        For each accumulated slice NALU:
          Write 4-byte big-endian length    ← AVCC length field
          Write raw NALU bytes              ← payload

Step 4: CMSampleBufferCreate() → VideoToolbox
```

**Pros:** Full H.264 parsing validates the bitstream. Catches malformed NALUs.
SPS/PPS changes are detected per-frame.

**Cons:** Two-pass (parse + reassemble). More complex. Tightly coupled to H264Decoder.

### Cobalt Starboard: Single-pass conversion

```
Location: starboard/shared/starboard/media/avc_util.cc

Step 1: GetAvccSizeFromAnnexB() scans buffer once
        For each start code found (3 or 4 bytes):
          output_size += 4 + payload_size

Step 2: Allocate CMBlockBuffer of exact AVCC size

Step 3: ConvertAnnexBToAvcc() scans buffer again:
        For each NALU:
          Detect start code size (3 or 4)
          Write 4-byte AVCC length field
          memcpy payload bytes

Step 4: CMSampleBufferCreateReady() → VideoToolbox
```

**Pros:** Simple, standalone, no parser dependency. Fast memcpy-based conversion.

**Cons:** Less validation before VideoToolbox. Trusts that upstream parser produced valid
AnnexB. Malformed data may fail later with poorer diagnostics (parameter-set parsing
failure, AnnexB conversion error, or VideoToolbox decode error).

### 3-byte Start Code Handling

| Aspect | Upstream Chromium | Cobalt Starboard |
|---|---|---|
| **Where** | `H264Parser::FindStartCode()` at `h264_parser.cc:367-401` | `GetAnnexBStartCodeSize()` at `avc_util.cc:38-46` |
| **Method** | Scans for `00 00 01`, then checks if preceded by `00` to upgrade to 4-byte | Checks for 4-byte first (`00 00 00 01`), then 3-byte (`00 00 01`) |
| **Result** | Both produce correct NALU boundaries | Both produce correct NALU boundaries |
| **Handles mixed?** | Yes — per-NALU detection | Yes — per-NALU detection |

---

## Audio: How Each Path Handles ADTS

### Upstream Chromium: FFmpeg or AudioToolbox

```
DecoderBuffer (ADTS or raw AAC)
    ↓
Normal AAC:                        xHE-AAC / AC3 / EAC3:
FFmpegAudioDecoder::Decode()       AudioToolboxAudioDecoder
    ↓                                  ↓
avcodec_send_packet()              Platform AudioToolbox
FFmpeg parses ADTS internally      handles format natively
    ↓                                  ↓
Decoded PCM audio                  Decoded PCM audio
```

Normal AAC generally uses FFmpeg, which natively understands ADTS framing.
xHE-AAC and some other codecs may use macOS AudioToolbox via `CanUseAudioToolbox()`.

### Cobalt Starboard: Manual ADTS stripping

```
DecoderBuffer (full ADTS frame)
    ↓
AacAVSampleBufferBuilder
    ↓
Check: data[0]==0xFF && (data[1]&0xF0)==0xF0 ?
    ↓ yes                    ↓ no
Skip 7 bytes             Use entire buffer
    ↓                        ↓
Raw AAC payload → CMSampleBuffer → CoreAudio
```

CoreAudio/AudioToolbox expects raw AAC frames (no ADTS header).
The builder must strip it manually.

---

## Could Cobalt Use Upstream's Approach?

### For Video (AnnexB → AVCC): Not directly, but conceptually possible

Upstream's path uses `H264Decoder` + `VideoToolboxH264Accelerator`. This is a full H.264
parsing pipeline where AnnexB→AVCC happens as a side effect of NALU-level decoding.

To adopt it, Cobalt would need to:
1. Use `H264Decoder` (from `media/gpu/h264_decoder.cc`) as the video decoder
2. Implement a `VideoToolboxH264Accelerator` for Starboard's AVSBDL layer
3. Remove the current `SbPlayerBridge` → `AvcAVVideoSampleBufferBuilder` path

**This is architecturally possible** but would be a large refactor. The current Starboard
pipeline bypasses Chromium's decoder stack entirely — it sends raw DecoderBuffers directly
to platform-specific builders via `SbPlayerBridge::WriteSamples()`.

**Trade-offs:**

| | Adopt Upstream (H264Decoder) | Keep Starboard (avc_util) |
|---|---|---|
| **Bitstream validation** | Yes — full H.264 parse | No — trusts upstream parser |
| **SPS/PPS change detection** | Per-frame | Per-keyframe |
| **Code complexity** | Higher — needs H264Decoder integration | Lower — standalone converter |
| **Maintenance** | Moves with upstream Chromium | Custom code to maintain |
| **Conversion correctness** | Proven in Chrome on macOS | Proven in Cobalt on tvOS |
| **Refactor effort** | Large — replace SbPlayer video path | None — already working |

### For Audio (ADTS stripping): No benefit

Upstream uses FFmpeg (normal AAC) or AudioToolbox (xHE-AAC) which handle ADTS/raw
framing internally. Cobalt creates CMSampleBuffers for the platform audio renderer /
CoreAudio path, which expects raw AAC. The current manual ADTS stripping is the right
architectural approach, though it could be hardened to parse the ADTS protection bit
and handle 9-byte headers (with CRC) instead of assuming a fixed 7 bytes.

### Recommendation

**Keep the current Starboard approach.** The `avc_util.cc` converter is correct, handles
all start code sizes, and is battle-tested. Adopting upstream's `H264Decoder` +
`VideoToolboxH264Accelerator` would provide bitstream validation but at significant
refactoring cost with no user-visible benefit for correct streams.

The only scenario where upstream's approach is clearly better: **malformed or adversarial
H.264 bitstreams**. Upstream is more likely to reject malformed streams earlier at the
H264Decoder parser level; Cobalt would pass them through to VideoToolbox where they may
be rejected by CoreMedia or produce decode errors with less informative diagnostics. For
a controlled app shell like Cobalt, this risk is low.

---

## Summary Table

| Aspect | Upstream Chromium | Cobalt Starboard | Same/Different |
|---|---|---|---|
| HLS demuxer | ManifestDemuxer + HlsManifestDemuxerEngine | Same architecture, not identical files | **Same architecture** |
| TS parser | EsParserH264 + EsParserAdts | Same architecture | **Same architecture** |
| fMP4 parser | MP4StreamParser + AVCBitstreamConverter | Same architecture | **Same architecture** |
| DecoderBuffer format | AnnexB video, ADTS audio (raw for xHE-AAC) | AnnexB video, ADTS audio (raw for xHE-AAC) | **Same** |
| Video decoder | H264Decoder → VideoToolboxH264Accelerator | SbPlayerBridge → AvcAVVideoSampleBufferBuilder | **Different** |
| AnnexB→AVCC | Inside accelerator, parse-then-reassemble | avc_util.cc, single-pass conversion | **Different impl, same result** |
| 3-byte SC support | H264Parser::FindStartCode() | GetAnnexBStartCodeSize() | **Both handle it** |
| Audio decoder | FFmpegAudioDecoder (normal AAC) or AudioToolbox (xHE-AAC/AC3/EAC3) | AacAVSampleBufferBuilder (manual ADTS strip) | **Different** |
| Final HW decode | VideoToolbox (via accelerator) | VideoToolbox (via AVSBDL) | **Same HW, different API layer** |
| Bitstream validation | Yes (H264Decoder parses every NALU) | Less — trusts upstream parser; failures surface later at CoreMedia/VideoToolbox | **Different** |
