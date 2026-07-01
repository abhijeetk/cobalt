# HLS Data Format Analysis: AnnexB Start Codes & AAC Headers

---

## How Does the Code Know What Format It's Looking At?

The Starboard tvOS sample buffer builders do not receive an explicit per-sample framing
enum. The higher-level pipeline has container/MIME/parser context, but by the time bytes
reach `SbPlayerBridge::WriteSamples()` → `InputBuffer`, that context is not carried as a
per-buffer field. So the builders must **infer framing from the sample bytes themselves**
— looking at the first few bytes to recognize patterns.

There are three detection points, each looking at different "magic bytes":

---

### Detection 1: Video — "Is this AnnexB or already AVCC?"

**Where:** `avc_av_video_sample_buffer_builder.mm:86-90`

```cpp
size_t avcc_size = GetAvccSizeFromAnnexB(source_data, data_size);
bool is_annex_b = (avcc_size > 0);
```

**How it works:** Look at the first bytes of the video buffer:

```
If buffer starts with:  00 00 00 01 ...  → AnnexB (4-byte start code)
If buffer starts with:  00 00 01 ...     → AnnexB (3-byte start code)
If buffer starts with:  anything else    → NOT AnnexB (assumed AVCC — see caveat below)
```

`GetAvccSizeFromAnnexB()` calls `GetAnnexBStartCodeSize()` which does this check:

```cpp
// avc_util.cc:38-46
size_t GetAnnexBStartCodeSize(const uint8_t* data, size_t size) {
  if (size >= 4 && memcmp(data, {0,0,0,1}, 4) == 0) return 4;  // found 00 00 00 01
  if (size >= 3 && memcmp(data, {0,0,1}, 3) == 0)   return 3;  // found 00 00 01
  return 0;                                                      // neither → not AnnexB
}
```

**Think of it like this:** You receive a package. If the label starts with "00 00 00 01"
or "00 00 01", it's AnnexB and needs conversion. Otherwise, it's **assumed** to already
be AVCC (length-prefixed) and is passed through unchanged. This is a fallback assumption,
not a validation — corrupt or unknown formats would also hit this path.

**What the code does with each case:**

```
┌─────────────────────────────────────────────────────────────┐
│               Video buffer arrives                          │
│               (just raw bytes, no label)                    │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
              ┌───────────────────┐
              │ First bytes are   │
              │ 00 00 [00] 01 ?   │
              └───────┬───────────┘
                      │
           ┌──── YES ─┴─ NO ────┐
           │                    │
           ▼                    ▼
   ┌───────────────┐    ┌───────────────────┐
   │ AnnexB format │    │ Assumed AVCC      │
   │               │    │ (fallback)        │
   │ 1. Calculate  │    │                   │
   │    AVCC size  │    │ Just memcpy the   │
   │ 2. Allocate   │    │ whole buffer.     │
   │    that size  │    │                   │
   │ 3. Convert    │    │ NOTE: this is an  │
   │    each NALU  │    │ assumption, not   │
   │               │    │ validation. A     │
   │               │    │ corrupt or unknown│
   │               │    │ format would also │
   │               │    │ hit this path.    │
   └───────────────┘    └───────────────────┘
```

---

### Detection 2: Video NALUs — "Is this an SPS, PPS, IDR, or AUD?"

**Where:** `avc_util.cc` inside `CreateFromAnnexB()`

Each NALU has a **type byte** right after the start code. The type is in the lower 5 bits:

```
After a start code (3 or 4 bytes), the next byte is the NALU header:

  Byte layout:  [F][NRI][  TYPE  ]
                 1   2    5 bits

  F   = forbidden_zero_bit (always 0)
  NRI = nal_ref_idc (0-3, varies by encoder)
  TYPE = nal_unit_type (what we care about)

Examples of the same NALU type with different NRI values:

  0x67 = 0 11 00111 → NRI=3, TYPE=7 (SPS)  ← most encoders
  0x27 = 0 01 00111 → NRI=1, TYPE=7 (SPS)  ← some encoders
  0x47 = 0 10 00111 → NRI=2, TYPE=7 (SPS)  ← also valid!

All three are SPS, but only 0x67 matched the old code.
```

**The fix — mask with 0x1F to extract only the type bits:**

```cpp
uint8_t nalu_type = nalu[sc_size] & 0x1F;  // 0x1F = 00011111 in binary
                                            // keeps only lower 5 bits

// 0x67 & 0x1F = 7 (SPS) ✓
// 0x27 & 0x1F = 7 (SPS) ✓
// 0x47 & 0x1F = 7 (SPS) ✓   ← all correctly detected now
```

**And `sc_size` tells us where the type byte is:**

```
3-byte start code:  [00 00 01] [67] [data...]
                               ↑ offset 3

4-byte start code:  [00 00 00 01] [67] [data...]
                                  ↑ offset 4

Old code always looked at offset 4 → WRONG for 3-byte start codes
New code: nalu[sc_size] → correct for both
```

---

### Detection 3: Audio — "Is this ADTS or raw AAC?"

**Where:** `av_audio_sample_buffer_builder.mm:75`

```cpp
bool is_adts = (size >= 7 && data[0] == 0xFF && (data[1] & 0xF0) == 0xF0);
```

**How it works:** ADTS frames always start with a **sync word** — 12 bits all set to 1:

```
ADTS header (first 2 bytes):

  Byte 0:  1111 1111  (0xFF)
  Byte 1:  1111 xxxx  (0xFx — upper 4 bits are 1, lower 4 vary)

  Together: 0xFFF = sync word (12 ones in a row)

So the check is:
  data[0] == 0xFF          → first byte all ones?
  data[1] & 0xF0 == 0xF0   → upper 4 bits of second byte all ones?

If BOTH true → this is an ADTS frame, skip the 7-byte header
If EITHER false → this is raw AAC, skip nothing
```

**Visual example:**

```
ADTS frame from MPEG-TS:

  Byte:  FF  F1  50  80  02  1F  FC  [AAC data starts here...]
         ^^  ^^
         sync word (0xFFF1 → upper 12 bits = FFF)

  is_adts = (0xFF == 0xFF) && (0xF1 & 0xF0 == 0xF0) → TRUE
  header_size = 7
  Skip first 7 bytes, feed "AAC data" to Apple decoder


Raw AAC frame (xHE-AAC from fMP4, or future direct Starboard paths):

  Byte:  21  19  45  00  48  ...  (actual AAC audio data)
         ^^
         0x21 is NOT 0xFF

  is_adts = (0x21 == 0xFF) → FALSE, stop checking
  header_size = 0
  Feed ALL bytes to Apple decoder (nothing to skip)

  Note: Normal (non-xHE) AAC from fMP4 is ADTS-wrapped by
  mp4_stream_parser.cc:89 (CreateAdtsFromEsds), so it looks
  like the ADTS case above.
```

```
┌─────────────────────────────────────────────────────────────┐
│               Audio buffer arrives                          │
│               (just raw bytes, no label)                    │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
              ┌───────────────────┐
              │ First 2 bytes     │
              │ match 0xFFF ?     │
              │ (FF Fx pattern)   │
              └───────┬───────────┘
                      │
           ┌──── YES ─┴─ NO ────┐
           │                    │
           ▼                    ▼
   ┌───────────────┐    ┌───────────────┐
   │ ADTS format   │    │ Raw AAC       │
   │ (TS or fMP4)  │    │ (xHE-AAC or  │
   │               │    │  future paths)│
   │               │    │               │
   │ Skip first    │    │ Feed entire   │
   │ 7 bytes       │    │ buffer to     │
   │ (ADTS header) │    │ Apple decoder │
   │ Feed rest to  │    │               │
   │ Apple decoder │    │               │
   └───────────────┘    └───────────────┘
```

---

### Why This Per-Buffer Detection is Important

The tvOS sample buffer builders detect format **independently for every single buffer**.
This means within a single stream, each buffer is examined fresh based on its bytes.

However, this is only the last stage. The higher-level HLS pipeline (`HlsManifestDemuxerEngine`)
selects a stream parser (MP2T vs MP4) per rendition based on the segment container type
(`hls_manifest_demuxer_engine.cc:892`). So format switching between TS and fMP4 mid-stream
depends on whether the HLS engine and `ChunkDemuxer` support a `ChangeType` path — not just
on the tvOS builders.

**What the per-buffer detection does guarantee:** Within either format (all-TS or all-fMP4),
the builders handle variation correctly — mixed 3/4-byte start codes in TS, ADTS vs raw AAC
(xHE-AAC), etc.

---

## Initial Prompt

> Why do we need commits 79961d199b28 (3-byte AnnexB), 408392f3553a (video sample buffer),
> 33aa88dd70cb (raw AAC)? What happens with format changes? Can the code handle all
> combinations of audio/video and NALU formats?

---

## The Two HLS Segment Formats

HLS can deliver segments in two container formats, each producing **different byte-level output**:

```
+---------------------------+       +---------------------------+
|     MPEG-TS Segments      |       |     fMP4 Segments         |
|     (.ts files)           |       |     (.m4s files)          |
+---------------------------+       +---------------------------+
| Video: AnnexB, mixed      |       | Video: AVCC (length-     |
|   3-byte & 4-byte start   |       |   prefixed), converted   |
|   codes, verbatim from    |       |   to AnnexB 4-byte by    |
|   encoder                 |       |   AVCBitstreamConverter   |
+---------------------------+       +---------------------------+
| Audio: Full ADTS frames   |       | Audio: ADTS-wrapped AAC  |
|   (7-byte header + AAC    |       |   (mp4_stream_parser.cc  |
|   payload)                |       |   adds ADTS header via   |
+---------------------------+       |   CreateAdtsFromEsds)    |
                                    |                          |
                                    | Exception: xHE-AAC is   |
                                    | passed as raw AAC (no    |
                                    | ADTS header added)       |
                                    +---------------------------+
```

---

## Video: Why 3-Byte Start Code Support is Needed

### What is AnnexB?

H.264 NALUs can be framed in two ways:

```
AnnexB Format (used in transport streams):
+------------------+------------+------------------+------------+
| Start Code       | NALU Data  | Start Code       | NALU Data  |
| 00 00 01 (3-byte)|            | 00 00 00 01      |            |
| or 00 00 00 01   |            | (4-byte)         |            |
| (4-byte)         |            |                  |            |
+------------------+------------+------------------+------------+

AVCC Format (used in MP4 containers & Apple decoders):
+------------------+------------+------------------+------------+
| Length (4 bytes)  | NALU Data  | Length (4 bytes)  | NALU Data  |
| big-endian        |            | big-endian        |            |
+------------------+------------+------------------+------------+
```

### MPEG-TS: Mixed Start Codes

MPEG-TS encoders typically use:
- **4-byte** (`00 00 00 01`) for AUD, SPS, PPS (parameter sets)
- **3-byte** (`00 00 01`) for IDR and non-IDR slices (actual frame data)

This is valid per H.264 spec and very common. A single access unit looks like:

```
Typical MPEG-TS H.264 Access Unit:

  00 00 00 01 [09 xx]          ← AUD (4-byte start code)
  00 00 00 01 [67 xx xx ...]   ← SPS (4-byte start code)  } only on
  00 00 00 01 [68 xx xx ...]   ← PPS (4-byte start code)  } keyframes
  00 00 01    [65 xx xx ...]   ← IDR slice (3-byte start code!)
                                  ^^^^^^^^
                                  This is the problem case
```

### fMP4: Always 4-Byte After Conversion

fMP4 stores AVCC (length-prefixed). Chromium's `AVC::ConvertFrameToAnnexB()` replaces
each 4-byte length with `00 00 00 01`. Result is always uniform 4-byte start codes.

```
fMP4 H.264 After Chromium Conversion:

  00 00 00 01 [67 xx xx ...]   ← SPS (4-byte, always)
  00 00 00 01 [68 xx xx ...]   ← PPS (4-byte, always)
  00 00 00 01 [65 xx xx ...]   ← IDR slice (4-byte, always)
```

### The Old Code Problem

The original `avc_util.cc` assumed ALL start codes are 4 bytes:

```
OLD CODE (broken for MPEG-TS):

  constexpr uint8_t kAnnexBHeader[] = {0, 0, 0, 1};     ← only 4-byte
  constexpr size_t kAnnexBHeaderSizeInBytes = 4;

  bool StartsWithAnnexBHeader(...) {
    return memcmp(data, kAnnexBHeader, 4) == 0;          ← misses 00 00 01
  }

  ConvertAnnexBToAvcc:
    payload_size = next_nalu - current_nalu - 4;          ← wrong for 3-byte
    avcc_destination += next_nalu - current_nalu;          ← size mismatch
```

What happens with a 3-byte start code NALU:

```
Input:   [00 00 01] [65 xx xx ... ] (3-byte start, IDR slice)
                ↓
Old code: StartsWithAnnexBHeader → FALSE (expects 00 00 00 01)
                ↓
Result:  ConvertAnnexBToAvcc returns false → VIDEO DROPPED

Or worse: if mixed in a buffer after a 4-byte NALU, the scanner
overshoots by 1 byte, corrupting the AVCC length field.
```

### The Fix (commit 79961d199b28)

```
NEW CODE:

  size_t GetAnnexBStartCodeSize(data, size) {
    if (size >= 4 && memcmp(data, {0,0,0,1}, 4) == 0) return 4;
    if (size >= 3 && memcmp(data, {0,0,1}, 3) == 0)   return 3;
    return 0;
  }

  ConvertAnnexBToAvcc:
    for each NALU:
      sc_size = GetAnnexBStartCodeSize(nalu_start)    ← 3 or 4
      payload_size = nalu_total_size - sc_size         ← correct for both
      write 4-byte AVCC length field
      copy payload (skipping sc_size bytes)
      advance by (4 + payload_size)                    ← AVCC is always 4+payload
```

### Why GetAvccSizeFromAnnexB is Needed (commit 408392f3553a)

When start codes are mixed, the AVCC output size **differs** from AnnexB input size:

```
Example: One NALU with 3-byte start code, 100 bytes payload

  AnnexB size:  3 + 100 = 103 bytes
  AVCC size:    4 + 100 = 104 bytes   ← 1 byte LARGER

Example: One NALU with 4-byte start code, 100 bytes payload

  AnnexB size:  4 + 100 = 104 bytes
  AVCC size:    4 + 100 = 104 bytes   ← same size

Example: Mixed buffer (common in MPEG-TS keyframes)

  NALU 1: 4-byte SC + 10 bytes (SPS)  → AnnexB: 14, AVCC: 14
  NALU 2: 4-byte SC + 8 bytes (PPS)   → AnnexB: 12, AVCC: 12
  NALU 3: 3-byte SC + 50000 bytes     → AnnexB: 50003, AVCC: 50004
                                                          ↑
  Total AnnexB: 50029                     differs!
  Total AVCC:   50030
```

The old video sample buffer builder allocated `CMBlockBuffer` with `data_size` (= AnnexB size).
With 3-byte start codes, the AVCC output is **larger**, causing buffer overflow:

```
OLD video builder (broken):

  CMBlockBufferCreateWithMemoryBlock(..., data_size, ...);  ← AnnexB size
  ConvertAnnexBToAvcc(source, data_size, block_data);       ← writes MORE than data_size!
                                                               BUFFER OVERFLOW

NEW video builder (fixed):

  avcc_size = GetAvccSizeFromAnnexB(source_data, data_size);
  CMBlockBufferCreateWithMemoryBlock(..., avcc_size, ...);  ← correct AVCC size
  ConvertAnnexBToAvcc(source, data_size, block_data);       ← fits perfectly
```

---

## Audio: Why Raw AAC Support is Needed

### ADTS vs Raw AAC

```
ADTS Frame (from MPEG-TS):
+-------------------+--------------------+
| ADTS Header       | AAC Payload        |
| 7 bytes           | (variable)         |
| FF F1 xx xx xx xx | [AAC frame data]   |
| ^^^^^^^^^^        |                    |
| sync word 0xFFF   |                    |
+-------------------+--------------------+

Raw AAC Frame (xHE-AAC from fMP4, or future Starboard paths):
+--------------------+
| AAC Payload        |
| (no header)        |
| [AAC frame data]   |
+--------------------+

Note: Normal (non-xHE) AAC from fMP4 is ADTS-wrapped by
mp4_stream_parser.cc:89 via CreateAdtsFromEsds(), so it
looks like the ADTS case above.
```

### The Old Code Problem

```
OLD audio builder:

  CMBlockBufferCreateWithMemoryBlock(
      data + kADTSHeaderSize,          ← ALWAYS skip 7 bytes
      size - kADTSHeaderSize, ...);    ← ALWAYS subtract 7

  When input is raw AAC (xHE-AAC from fMP4, or any future path
  that delivers AAC without ADTS wrapping):
    First 7 bytes of actual audio data are SKIPPED
    → corrupted audio → silence or noise
```

### The Fix (commit 33aa88dd70cb)

```
NEW audio builder:

  bool is_adts = (size >= 7 && data[0] == 0xFF && (data[1] & 0xF0) == 0xF0);
  size_t header_size = is_adts ? kADTSHeaderSize : 0;
                                  ↑                ↑
                          ADTS path    raw AAC / xHE-AAC path

  CMBlockBufferCreateWithMemoryBlock(
      data + header_size,              ← skip 7 or 0
      size - header_size, ...);
```

---

## Complete Pipeline: Format at Each Stage

```
┌─────────────────────────────────────────────────────────────────────┐
│                        HLS Manifest (.m3u8)                        │
│              HlsManifestDemuxerEngine parses playlist               │
│              Selects variant, fetches segments                      │
└──────────────────────┬──────────────────────┬───────────────────────┘
                       │                      │
              MPEG-TS (.ts)            fMP4 (.m4s)
                       │                      │
                       ▼                      ▼
┌──────────────────────────────┐  ┌──────────────────────────────────┐
│  MP2T Stream Parser          │  │  MP4 Stream Parser               │
│                              │  │                                  │
│  Video: es_parser_h264.cc    │  │  Video: mp4_stream_parser.cc     │
│    Raw AnnexB, mixed 3/4     │  │    AVCC → AnnexB 4-byte only     │
│    byte start codes          │  │    (AVCBitstreamConverter)        │
│                              │  │                                  │
│  Audio: es_parser_adts.cc    │  │  Audio: mp4_stream_parser.cc     │
│    Full ADTS (7-byte hdr     │  │    ADTS-wrapped AAC (via         │
│    + AAC payload)            │  │    CreateAdtsFromEsds)            │
│                              │  │    Exception: xHE-AAC = raw      │
└──────────────┬───────────────┘  └──────────────────┬───────────────┘
               │                                     │
               ▼                                     ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      ChunkDemuxer                                   │
│              Stores buffers in SourceBufferStream                    │
│              NO format conversion — pass-through                    │
└──────────────────────────────┬──────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│                   ManifestDemuxerStream                              │
│              Wraps ChunkDemuxerStream                                │
│              NO format conversion — pass-through                    │
└──────────────────────────────┬──────────────────────────────────────┘
                               │
                          Mojo IPC
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│                   StarboardRenderer                                 │
│              Reads from DemuxerStream                                │
│              Feeds DecoderBuffers to SbPlayerBridge                  │
│              NO format conversion — pass-through                    │
└──────────────────────────────┬──────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      SbPlayerBridge                                  │
│              WriteSamples() → InputBuffer                           │
│              NO format conversion — pass-through                    │
└──────────────┬───────────────────────────────────┬──────────────────┘
               │                                   │
               ▼                                   ▼
┌──────────────────────────────┐  ┌──────────────────────────────────┐
│  AvcAVVideoSampleBufferBuilder│  │  AacAVSampleBufferBuilder       │
│  (tvOS)                      │  │  (tvOS)                          │
│                              │  │                                  │
│  1. On keyframe: parse       │  │  1. Detect ADTS sync (0xFFF)    │
│     AvcParameterSets from    │  │  2. If ADTS: skip 7-byte header │
│     AnnexB (handles 3/4      │  │     If raw: skip 0 bytes        │
│     byte start codes)        │  │  3. Create CMBlockBuffer with   │
│  2. Skip parameter set bytes │  │     AAC payload only            │
│  3. GetAvccSizeFromAnnexB()  │  │  4. Create CMSampleBuffer       │
│     to compute output size   │  │                                  │
│  4. ConvertAnnexBToAvcc()    │  │  Apple AudioToolbox decodes     │
│     (handles mixed 3/4 byte) │  │                                  │
│  5. Create CMSampleBuffer    │  └──────────────────────────────────┘
│                              │
│  Apple VideoToolbox decodes  │
└──────────────────────────────┘
```

---

## All Format Combinations Matrix

| HLS Segment Type | Video Input Format | Audio Input Format | Video Builder Action | Audio Builder Action |
|---|---|---|---|---|
| **MPEG-TS** (.ts) | AnnexB mixed 3/4-byte SC | ADTS (7-byte hdr + AAC) | GetAvccSizeFromAnnexB → ConvertAnnexBToAvcc | Detect ADTS → skip 7 bytes |
| **fMP4** (.m4s) | AnnexB uniform 4-byte SC | ADTS (added by mp4_stream_parser via CreateAdtsFromEsds) | GetAvccSizeFromAnnexB → ConvertAnnexBToAvcc | Detect ADTS → skip 7 bytes |
| **fMP4 xHE-AAC** | N/A | Raw AAC (no ADTS — xHE-AAC bypasses CreateAdtsFromEsds) | N/A | No ADTS detected → skip 0 bytes |
| **fMP4 audio-only** | N/A | ADTS (normal AAC) or raw (xHE-AAC) | N/A | Per-buffer ADTS detection |
| **MPEG-TS audio-only** | N/A | ADTS | N/A | Detect ADTS → skip 7 bytes |

### Edge Cases Handled

| Scenario | Video | Audio | Status |
|---|---|---|---|
| TS with all 3-byte SCs | GetAnnexBStartCodeSize returns 3, AVCC size computed correctly | N/A | ✅ Handled |
| TS with all 4-byte SCs | GetAnnexBStartCodeSize returns 4, same as old behavior | N/A | ✅ Handled |
| TS with mixed 3/4-byte SCs | Each NALU's SC size detected individually | N/A | ✅ Handled |
| fMP4 (always 4-byte) | GetAnnexBStartCodeSize returns 4 | ADTS (normal AAC) or raw (xHE-AAC), detected per-buffer | ✅ Handled |
| Already-AVCC data (no SC) | GetAvccSizeFromAnnexB returns 0, is_annex_b=false, memcpy passthrough | N/A | ✅ Handled |
| ADTS with 9-byte header (CRC) | The MPEG-TS parser (`es_parser_adts.cc:39`) already handles 7 vs 9-byte ADTS via the protection_absent bit, so buffers reaching the tvOS builder should always have correct framing. If a 9-byte ADTS somehow reaches the builder directly, the builder currently only skips 7 bytes — the builder could be improved to parse the protection bit. | | ⚠️ Low risk — upstream parser handles it |
| Format change mid-stream (TS→fMP4 or vice versa) | tvOS builders detect per-buffer, but **the HLS engine selects a stream parser per rendition** (`hls_manifest_demuxer_engine.cc:892`). Arbitrary mid-stream container switching requires a ChangeType/role recreation path at the ChunkDemuxer level, which may not exist. | | ⚠️ tvOS builders handle it; higher-level pipeline may not |

### Note on TS/fMP4 Variant Switching (Not Proven Supported)

HLS ABR can theoretically have variants with different container formats (e.g., low-bitrate
TS, high-bitrate fMP4). However, **this implementation does not appear to support mid-stream
container switching**. The HLS engine selects a stream parser per rendition when it is
created (`hls_manifest_demuxer_engine.cc:838, :858`), and there is no evidence of a
`ChangeType` path that would allow switching from MP2T to MP4 parser within the same
`ChunkDemuxer` source buffer.

The tvOS sample buffer builders *would* handle mixed-format buffers correctly (per-buffer
detection), but the higher-level pipeline may not deliver them in practice.

---

## NALU Type Detection: Old vs New

The old code matched full bytes for NALU types:

```
OLD:  if (nalu[4] == 0x67)  → SPS     (only works with 4-byte SC at offset 4)
      if (nalu[4] == 0x68)  → PPS
      if (nalu[4] == 0x65)  → IDR
      if (nalu[4] == 0x09)  → AUD

Problem: 0x67 = nal_ref_idc=3, nal_unit_type=7 (SPS)
         But some encoders use nal_ref_idc=1: 0x27 = nal_ref_idc=1, nal_unit_type=7

         The old code would MISS this SPS because 0x27 != 0x67!
```

The new code uses proper masking:

```
NEW:  sc_size = GetAnnexBStartCodeSize(nalu.data(), nalu.size())  ← 3 or 4
      nalu_type = nalu[sc_size] & 0x1F                            ← mask lower 5 bits

      if (nalu_type == 7)  → SPS     (works regardless of nal_ref_idc)
      if (nalu_type == 8)  → PPS
      if (nalu_type == 5)  → IDR
      if (nalu_type == 9)  → AUD

This correctly identifies NALUs regardless of:
  1. Start code size (3 or 4 bytes → variable offset)
  2. nal_ref_idc value (upper 3 bits vary per encoder)
```

---

## Summary: Why Each Commit is Needed

### 79961d199b28 — 3-byte AnnexB start codes in avc_util

**Without this:** MPEG-TS HLS video frames with 3-byte start codes fail
`ConvertAnnexBToAvcc()` → no video. Also, NALU type detection using full-byte
compare misses NALUs with non-standard `nal_ref_idc` values.

**What it fixes:** All AnnexB parsing functions now handle both 3-byte and 4-byte
start codes. NALU type detection uses proper `& 0x1F` masking.

### 408392f3553a — Video sample buffer sizing for MPEG-TS HLS

**Without this:** When 3-byte start codes are converted to 4-byte AVCC length fields,
the output is larger than the input. The CMBlockBuffer is allocated with the input size
→ buffer overflow or truncated data.

**What it fixes:** Pre-calculates correct AVCC output size with `GetAvccSizeFromAnnexB()`.
Also handles the case where data is already AVCC (no AnnexB start codes detected).

### 33aa88dd70cb — Raw AAC support in audio sample buffer builder

**Without this:** The builder blindly skips 7 bytes assuming every frame has an ADTS header.
For normal AAC from both MPEG-TS and fMP4, this works because `mp4_stream_parser.cc:89`
wraps fMP4 AAC samples with ADTS via `CreateAdtsFromEsds()`. However, **xHE-AAC bypasses
this wrapping** (`mp4_stream_parser.cc:88-91`) and arrives as raw AAC — the old code would
corrupt it by skipping 7 bytes of actual audio data.

**What it fixes:** The builder should not blindly skip 7 bytes unless the frame actually
begins with an ADTS sync word. This is a defensive, correct approach that handles:
- Normal AAC from MPEG-TS (ADTS present → skip 7 bytes)
- Normal AAC from fMP4 (ADTS added by parser → skip 7 bytes)
- xHE-AAC from fMP4 (no ADTS → skip 0 bytes)
- Any future Starboard path that might deliver raw AAC
