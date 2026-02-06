# Cobalt libvpx Removal Architecture

**Document Version:** 1.0
**Last Updated:** 2026-02-06
**Bug Reference:** b/421171908
**Related Commit:** 6226acf18d26066d015a29f96e26c5b8b07f6ee2

---

## Executive Summary

This document explains the technical rationale for removing libvpx (VP8/VP9 video codecs) dependencies from Cobalt's WebRTC integration and the architectural decisions made to accomplish this.

**Key Findings:**
- Binary size reduction: ~700KB (libvpx) + ~200KB (encoder code) = ~900KB total savings
- Cobalt is playback-only and does not require video encoding
- Upstream WebRTC architecture made clean removal complex
- Stub replacement was the most pragmatic solution

---

## Table of Contents

1. [Background](#background)
2. [Problem Statement](#problem-statement)
3. [Why Existing Flags Don't Work](#why-existing-flags-dont-work)
4. [The Solution: Stub Approach](#the-solution-stub-approach)
5. [Encoder vs Decoder Treatment](#encoder-vs-decoder-treatment)
6. [Runtime Usage Analysis](#runtime-usage-analysis)
7. [Alternatives Considered](#alternatives-considered)
8. [Implementation Details](#implementation-details)
9. [Future Recommendations](#future-recommendations)
10. [References](#references)

---

## Background

### Cobalt's WebRTC Usage

Cobalt supports a minimal subset of WebRTC functionality:

| Feature | Support | Purpose |
|---------|---------|---------|
| **getUserMedia (audio)** | ✅ Supported | Microphone capture for voice input |
| **getUserMedia (video)** | ❌ Denied | No camera access |
| **RTCPeerConnection** | ❌ Not used | No peer-to-peer communication |
| **MediaRecorder** | ❌ Not used | No video recording |
| **RTCDataChannel** | ❌ Not used | No data channels |

**Key Insight:** Cobalt only needs audio capture, not video encoding/decoding via WebRTC.

### Video Codec Requirements

| Codec | Encoder Needed? | Decoder Needed? | Rationale |
|-------|-----------------|-----------------|-----------|
| **VP8** | ❌ No | ❌ No | Not used for video playback |
| **VP9** | ❌ No | ❌ No | Not used for video playback |
| **H264** | ❌ No | ⚠️ Maybe | May be used for video playback elsewhere |
| **AV1** | ❌ No | ⚠️ Maybe | May be used for video playback elsewhere |

---

## Problem Statement

### Goal
Remove VP8 and VP9 codec support from WebRTC to reduce binary size while maintaining audio capture functionality.

### Challenges

1. **VP8 is Hardcoded in WebRTC**
   - VP8 was designed as the mandatory baseline codec for WebRTC 1.0
   - No `RTC_ENABLE_VP8` preprocessor define exists (unlike VP9, H264, AV1)
   - Unconditional includes in encoder factory

2. **Template-Based Architecture**
   - Encoder factory uses compile-time templates with codec adapters
   - Cannot conditionally remove template parameters without messy preprocessor logic

3. **Existing Flags Insufficient**
   - `rtc_build_libvpx` only controls linking, not compilation
   - Setting it to `false` still requires VP8/VP9 headers to exist

---

## Why Existing Flags Don't Work

### Available Flags

WebRTC provides two libvpx-related flags:

```gn
# third_party/webrtc/webrtc.gni
rtc_build_libvpx = !build_with_mozilla       # Controls library linking
rtc_libvpx_build_vp9 = !build_with_mozilla   # Controls VP9 features
```

### What They Control

| Flag | What It Does | What It Doesn't Do |
|------|--------------|---------------------|
| `rtc_build_libvpx` | Adds/removes `deps += [rtc_libvpx_dir]` | ❌ Does NOT remove includes |
| `rtc_libvpx_build_vp9` | Defines `RTC_ENABLE_VP9` | ❌ Does NOT affect VP8 |

### Why They're Insufficient for Removal

#### Problem 1: Unconditional Includes

```cpp
// third_party/webrtc/media/engine/internal_encoder_factory.cc

// ❌ NO #ifdef guards!
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"

// ✅ Has #ifdef guards
#if defined(WEBRTC_USE_H264)
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"
#endif
```

Even with `rtc_build_libvpx = false`, the compiler still tries to `#include` these headers.

#### Problem 2: Template Parameters

```cpp
using Factory = VideoEncoderFactoryTemplate<
    LibvpxVp8EncoderTemplateAdapter,        // ❌ Hardcoded in template
    LibvpxVp9EncoderTemplateAdapter         // ❌ Hardcoded in template
>;
```

Template parameters are evaluated at compile time. Cannot remove them without preprocessor conditionals.

#### Problem 3: No VP8 Conditional Define

```cpp
// third_party/webrtc/BUILD.gn:334

if (rtc_libvpx_build_vp9) {
  defines += [ "RTC_ENABLE_VP9" ]      // ✅ VP9 has this
}

if (rtc_use_h264) {
  defines += [ "WEBRTC_USE_H264" ]     // ✅ H264 has this
}

// ❌ NO equivalent for VP8!
```

### Use Case for Flags

The flags were designed for **external provision**, not **removal**:

1. **Mozilla/Firefox Integration** (`build_with_mozilla = true`)
   - Firefox provides its own libvpx library
   - WebRTC links against Firefox's libvpx instead of bundling its own
   - VP8/VP9 code still compiles

2. **System Package Integration** (Linux)
   - Use system-provided libvpx from package manager
   - Example: `pkg-config vpx`
   - VP8/VP9 code still compiles

3. **Hardware-Only Encoding** (Theoretical)
   - Use MediaCodec (Android) or VideoToolbox (iOS)
   - Disable software codecs
   - But Chromium doesn't use these flags this way

**Conclusion:** The flags were never designed for complete codec removal.

---

## The Solution: Stub Approach

### What We Did

```gn
# third_party/webrtc/media/BUILD.gn:578-581

rtc_library("rtc_internal_video_codecs") {
  sources = [
    "engine/internal_encoder_factory.cc",
    "engine/internal_encoder_factory.h",
  ]

  if (is_starboard) {
    sources -= [ "engine/internal_encoder_factory.cc" ]
    sources += [ "engine/internal_encoder_factory_starboard_stub.cc" ]

    deps -= [
      "../api/video_codecs:video_encoder_factory_template_libvpx_vp8_adapter",
      "../api/video_codecs:video_encoder_factory_template_libvpx_vp9_adapter",
      "../modules/video_coding:webrtc_vp8",
      "../modules/video_coding:webrtc_vp9",
    ]
  }
}
```

### Stub Implementation

```cpp
// internal_encoder_factory_starboard_stub.cc

std::vector<SdpVideoFormat> InternalEncoderFactory::GetSupportedFormats() const {
  return {};  // No codecs available
}

std::unique_ptr<VideoEncoder> InternalEncoderFactory::Create(
    const Environment& env,
    const SdpVideoFormat& format) {
  return nullptr;  // Cannot create encoder
}

VideoEncoderFactory::CodecSupport InternalEncoderFactory::QueryCodecSupport(
    const SdpVideoFormat& format,
    std::optional<std::string> scalability_mode) const {
  return {.is_supported = false};  // All codecs unsupported
}
```

### Benefits

| Benefit | Explanation |
|---------|-------------|
| ✅ **Clean Separation** | Starboard changes isolated to one file |
| ✅ **No Preprocessor Complexity** | No #ifdef soup in shared code |
| ✅ **No Upstream Conflicts** | Original file unchanged, easy to merge updates |
| ✅ **Binary Size Savings** | Removed ~900KB of unused code |
| ✅ **Future Flexibility** | Easy to add custom encoder later |
| ✅ **Clear Intent** | Obvious this is a no-op implementation |

### Drawbacks

| Drawback | Mitigation |
|----------|------------|
| ⚠️ **Duplicate File** | Only 44 lines, minimal maintenance |
| ⚠️ **Not Upstream** | Documented, can be proposed upstream |
| ⚠️ **Build Complexity** | Well-documented in BUILD.gn |

---

## Encoder vs Decoder Treatment

### Why Different Approaches?

| Aspect | Encoder | Decoder |
|--------|---------|---------|
| **Implementation** | Template-based factory | Runtime if-statement dispatch |
| **Codec Removal** | Remove ALL codecs | Remove SOME codecs (keep H264) |
| **Upstream Design** | VP8 hardcoded as mandatory | All codecs optional |
| **Conditional Compilation** | Complex (template parameters) | Simple (if-statements) |
| **Solution** | Complete file replacement (stub) | Conditional compilation (#ifdef) |

### Decoder Implementation

The decoder uses simple runtime dispatch, making conditional compilation clean:

```cpp
// third_party/webrtc/media/engine/internal_decoder_factory.cc

std::vector<SdpVideoFormat> InternalDecoderFactory::GetSupportedFormats() const {
  std::vector<SdpVideoFormat> formats;

#if !BUILDFLAG(IS_STARBOARD)
  formats.push_back(SdpVideoFormat::VP8());           // Remove for Starboard
  for (const auto& format : SupportedVP9DecoderCodecs())
    formats.push_back(format);                         // Remove for Starboard
#endif

  for (const auto& format : SupportedH264DecoderCodecs())
    formats.push_back(format);                         // ✅ Keep H264

  return formats;
}

std::unique_ptr<VideoDecoder> InternalDecoderFactory::Create(
    const Environment& env,
    const SdpVideoFormat& format) {

#if !BUILDFLAG(IS_STARBOARD)
  if (absl::EqualsIgnoreCase(format.name, kVp8CodecName))
    return CreateVp8Decoder(env);                      // Remove for Starboard
  if (absl::EqualsIgnoreCase(format.name, kVp9CodecName))
    return VP9Decoder::Create();                       // Remove for Starboard
#endif

  if (absl::EqualsIgnoreCase(format.name, kH264CodecName))
    return H264Decoder::Create();                      // ✅ Keep H264

  return nullptr;
}
```

**Why This Works for Decoder:**
1. No template complexity
2. Simple if-statements
3. Keeps H264/AV1 for potential video playback use
4. Clean, surgical removal of VP8/VP9

### Could We Use Conditionals for Encoder?

Theoretically yes, but it would be messy:

```cpp
// Hypothetical conditional approach (NOT USED)

#if defined(RTC_ENABLE_VP8)  // ❌ This define doesn't exist!
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#endif

using Factory = VideoEncoderFactoryTemplate<
#if defined(RTC_ENABLE_VP8)
    LibvpxVp8EncoderTemplateAdapter,  // Trailing comma problem!
#endif
#if defined(WEBRTC_USE_H264)
    OpenH264EncoderTemplateAdapter,   // Trailing comma problem!
#endif
    void  // Dummy to handle empty case
>;
```

**Problems:**
1. Trailing comma management across all conditional combinations
2. Need template specialization for empty case
3. More complex than 44-line stub
4. Harder to review in code reviews
5. Merge conflicts on every upstream encoder factory change

**Decision:** Stub is cleaner and more maintainable.

---

## Runtime Usage Analysis

### When Is the Stub Called?

The stub is instantiated in:
```cpp
// third_party/blink/renderer/platform/peerconnection/video_codec_factory.cc:156

class EncoderAdapter : public webrtc::VideoEncoderFactory {
 private:
  webrtc::InternalEncoderFactory software_encoder_factory_;  // ← Stub instance
};
```

### Call Stack

```
1. JavaScript: new RTCPeerConnection()
   ↓
2. Blink: PeerConnectionDependencyFactory::CreatePeerConnection()
   ↓
3. Blink: PeerConnectionDependencyFactory::GetPcFactory()
   ↓
4. Blink: PeerConnectionDependencyFactory::CreatePeerConnectionFactory()
   ↓
5. Blink: CreateWebrtcVideoEncoderFactory()
   ↓
6. Blink: new EncoderAdapter()
   ↓
7. WebRTC: InternalEncoderFactory constructor  ← STUB INSTANTIATED
```

### When Methods Are Called

| Method | Called When | Cobalt Usage |
|--------|-------------|--------------|
| `GetSupportedFormats()` | Querying RTP capabilities, creating PeerConnection | ❌ Never in production |
| `Create()` | Encoding video frame via WebRTC | ❌ Never in production |
| `QueryCodecSupport()` | Checking codec support for SDP negotiation | ❌ Never in production |

### Production Usage in Cobalt

```cpp
// cobalt/shell/browser/shell.cc:843

void Shell::RequestMediaAccessPermission(...) {
  if (request.audio_type != blink::mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE) {
    // ❌ VIDEO DENIED - No video capture allowed
    std::move(callback).Run(..., PERMISSION_DENIED, ...);
    return;
  }
  // ✅ Only audio capture is allowed
}
```

**Findings:**
- ❌ Cobalt does NOT use RTCPeerConnection
- ❌ Cobalt does NOT use MediaRecorder
- ❌ Cobalt does NOT encode video
- ✅ Stub is compiled and linked
- ⚠️ Stub is INSTANTIATED only if PeerConnection is created
- ⚠️ Stub methods are NEVER CALLED in production

### Is It Dead Code?

**Not exactly:**

| Scenario | Is Stub Used? |
|----------|---------------|
| **Production Cobalt** | Compiled ✅, Linked ✅, Instantiated ❌, Called ❌ |
| **Browser Tests** | May instantiate PeerConnections → Stub instantiated |
| **Future Proofing** | Prevents crashes if RTCPeerConnection accidentally accessed |
| **Module Dependency** | `peerconnection` module requires encoder factory member |

### Binary Size

```bash
# Object file sizes
internal_encoder_factory.o:              211 KB  (removed)
internal_encoder_factory_starboard_stub.o: 7.7 KB  (added)
libvpx library:                          ~700 KB  (removed)

# Net savings: ~900 KB
```

---

## Alternatives Considered

### Alternative 1: Conditional Compilation in Original File

```cpp
// NOT USED - Too messy

#if !BUILDFLAG(IS_STARBOARD)
#include "...libvpx_vp8_adapter.h"
#include "...libvpx_vp9_adapter.h"
#endif

using Factory = VideoEncoderFactoryTemplate<
#if !BUILDFLAG(IS_STARBOARD)
    LibvpxVp8EncoderTemplateAdapter,
    LibvpxVp9EncoderTemplateAdapter,
#endif
    void  // Empty case
>;
```

**Pros:**
- Single file
- No stub needed

**Cons:**
- ❌ Trailing comma complexity
- ❌ Template specialization needed for empty case
- ❌ Preprocessor soup
- ❌ Merge conflicts on every upstream change
- ❌ Less readable

### Alternative 2: Add RTC_ENABLE_VP8 Define Upstream

```gn
# Propose to WebRTC upstream

if (rtc_build_libvpx) {
  defines += [ "RTC_ENABLE_VP8" ]  # NEW!
}
```

**Pros:**
- ✅ Consistent with VP9/H264/AV1
- ✅ Helps all embedders
- ✅ Clean solution

**Cons:**
- ⏱️ Requires upstream buy-in
- ⏱️ Months to land and propagate
- ⏱️ Still need workaround in meantime
- ❌ Upstream may reject (VP8 is mandatory by design)

**Status:** Could be proposed, but stub needed anyway for current releases.

### Alternative 3: Runtime Codec Registry

```cpp
// NOT USED - Too large a refactor

class VideoEncoderRegistry {
 public:
  void RegisterEncoder(SdpVideoFormat, CreateFunc);
  std::unique_ptr<VideoEncoder> Create(...);
};

// Codecs self-register when linked
VP8Registrar g_vp8_registrar;  // Removed if not linked
```

**Pros:**
- ✅ No templates
- ✅ Clean runtime extension
- ✅ No preprocessor needed
- ✅ Modern architecture

**Cons:**
- ❌ Massive upstream refactor
- ❌ Backward compatibility challenges
- ❌ Initialization order concerns
- ❌ Runtime overhead (registry lookup)
- ⏱️ Years to implement

**Status:** Ideal long-term solution, impractical short-term.

### Alternative 4: Remove PeerConnection Module

```gn
# NOT USED - Too broad

blink_modules_sources("modules") {
  sources = [
    # Remove entire peerconnection module
    # "//third_party/blink/renderer/modules/peerconnection",  # REMOVED
  ]
}
```

**Pros:**
- ✅ Complete removal
- ✅ Maximum binary savings

**Cons:**
- ❌ Breaks browser tests that use RTCPeerConnection
- ❌ Removes getUserMedia infrastructure (needed for audio!)
- ❌ Prevents future WebRTC features
- ❌ Too invasive

**Status:** Out of scope. Removes more than necessary.

### Decision Matrix

| Alternative | Complexity | Maintainability | Binary Savings | Timeline |
|-------------|-----------|-----------------|----------------|----------|
| **Stub (CHOSEN)** | Low | High | ~900 KB | ✅ Immediate |
| Conditional Compilation | Medium | Low | ~900 KB | ✅ Immediate |
| Upstream RTC_ENABLE_VP8 | Low | High | ~900 KB | ⏱️ 6+ months |
| Runtime Registry | Very High | Very High | ~900 KB | ⏱️ 2+ years |
| Remove PeerConnection | High | Medium | ~2 MB | ⚠️ Breaks tests |

---

## Implementation Details

### Files Modified

#### 1. WebRTC Media BUILD.gn

```gn
# third_party/webrtc/media/BUILD.gn

rtc_library("rtc_internal_video_codecs") {
  # Line 570-576: Original sources
  sources = [
    "engine/internal_encoder_factory.cc",
    "engine/internal_encoder_factory.h",
  ]

  # Line 578-581: Starboard substitution
  if (is_starboard) {
    sources -= [ "engine/internal_encoder_factory.cc" ]
    sources += [ "engine/internal_encoder_factory_starboard_stub.cc" ]

    # Also remove adapter dependencies
    deps -= [
      "../api/video_codecs:video_encoder_factory_template_libvpx_vp8_adapter",
      "../api/video_codecs:video_encoder_factory_template_libvpx_vp9_adapter",
      "../modules/video_coding:webrtc_vp8",
      "../modules/video_coding:webrtc_vp9",
    ]
  }
}
```

**Key Points:**
- Clean conditional based on `is_starboard`
- File substitution, not #ifdef
- Also removes deps to prevent link errors

#### 2. Internal Decoder Factory (Conditional)

```cpp
// third_party/webrtc/media/engine/internal_decoder_factory.cc

#if !BUILDFLAG(IS_STARBOARD)
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "modules/video_coding/codecs/vp9/include/vp9.h"
#endif

std::unique_ptr<VideoDecoder> InternalDecoderFactory::Create(...) {
#if !BUILDFLAG(IS_STARBOARD)
  if (absl::EqualsIgnoreCase(format.name, kVp8CodecName))
    return CreateVp8Decoder(env);
  if (absl::EqualsIgnoreCase(format.name, kVp9CodecName))
    return VP9Decoder::Create();
#endif
  // H264 and AV1 still available
}
```

**Why Different:** Simple if-statements, keeps some codecs.

#### 3. Blink MediaRecorder BUILD.gn

```gn
# third_party/blink/renderer/modules/mediarecorder/BUILD.gn

blink_modules_sources("mediarecorder") {
  if (is_starboard) {
    # Removed vpx_encoder sources (commented out in commit)
    deps -= [ "//third_party/libvpx" ]
  }
}
```

#### 4. WebRTC Tools BUILD.gn

```gn
# third_party/webrtc/rtc_tools/BUILD.gn

if (is_starboard) {
  deps -= [
    "../api/video_codecs:video_decoder_factory_template_libvpx_vp8_adapter",
    "../api/video_codecs:video_encoder_factory_template_libvpx_vp8_adapter",
    "../api/video_codecs:video_decoder_factory_template_libvpx_vp9_adapter",
    "../api/video_codecs:video_encoder_factory_template_libvpx_vp9_adapter",
  ]
}
```

#### 5. WebRTC Test BUILD.gn

```gn
# third_party/webrtc/test/BUILD.gn

if (is_starboard) {
  deps -= [
    "../modules/video_coding:webrtc_vp8",
    "../modules/video_coding:webrtc_vp9"
  ]
}
```

### Build Verification

```bash
# Verify stub is used
$ ar t out/linux-x64x11_debug/obj/third_party/webrtc/media/librtc_internal_video_codecs.a \
  | grep internal_encoder_factory
internal_encoder_factory_starboard_stub.o

# Verify stub is in binary
$ strings out/linux-x64x11_debug/cobalt | grep internal_encoder_factory
internal_encoder_factory_starboard_stub.cc

# Verify symbols
$ nm out/linux-x64x11_debug/cobalt | grep "InternalEncoderFactory::GetSupportedFormats"
[Address] T _ZNK6webrtc22InternalEncoderFactory19GetSupportedFormatsEv
```

---

## Future Recommendations

### Short Term (Current)

✅ **Keep the stub approach**
- Well-documented
- Minimal maintenance
- Clear intent

### Medium Term (6-12 months)

📝 **Propose RTC_ENABLE_VP8 to upstream WebRTC**

```cpp
// Proposal for third_party/webrtc/BUILD.gn

config("common_config") {
  defines = []

  if (rtc_build_libvpx) {
    defines += [ "RTC_ENABLE_VP8" ]  // NEW!
  }

  if (rtc_libvpx_build_vp9) {
    defines += [ "RTC_ENABLE_VP9" ]
  }
}
```

**Benefits:**
- Consistent treatment of all codecs
- Helps all embedders (not just Cobalt)
- Enables clean conditional compilation

**Pitch to Upstream:**
> "VP8 is mandatory for standard WebRTC, but embedders like Cobalt, Firefox,
> and custom implementations need flexibility. Adding RTC_ENABLE_VP8 doesn't
> change default behavior but enables cleaner customization."

### Long Term (2+ years)

🏗️ **Advocate for Runtime Codec Registry**

Modern codec frameworks (FFmpeg, GStreamer, Android MediaCodec) use plugin-style architectures:

```cpp
// Ideal future architecture

class VideoCodecPlugin {
 public:
  virtual std::string Name() = 0;
  virtual std::unique_ptr<VideoEncoder> CreateEncoder(...) = 0;
  virtual std::unique_ptr<VideoDecoder> CreateDecoder(...) = 0;
};

class VideoCodecManager {
 public:
  void RegisterPlugin(std::unique_ptr<VideoCodecPlugin>);
  VideoEncoderFactory* GetEncoderFactory();
  VideoDecoderFactory* GetDecoderFactory();
};

// Usage
int main() {
  VideoCodecManager::RegisterPlugin(std::make_unique<VP8Plugin>());   // Optional
  VideoCodecManager::RegisterPlugin(std::make_unique<H264Plugin>());  // Optional
  // Platform chooses which codecs to load
}
```

**Benefits:**
- ✅ Clean separation of concerns
- ✅ Platform has full control
- ✅ No build system magic
- ✅ Easy testing with mock plugins
- ✅ Runtime extensibility

**Drawbacks:**
- Large refactor
- Backward compatibility challenges
- Requires multi-year effort

### If Cobalt Needs Encoding Later

If Cobalt ever needs video encoding (e.g., MediaRecorder support):

```cpp
// Replace stub with platform-specific factory

class StarboardEncoderFactory : public VideoEncoderFactory {
 public:
  std::unique_ptr<VideoEncoder> Create(...) override {
    // Use MediaCodec on Android
    // Use VideoToolbox on iOS
    // Use hardware encoders, not software
  }
};
```

---

## References

### Commits

- **6226acf18d26** - "Nolibvpx (#8468)" - Initial VP8/VP9 removal
- **f04898578f5b** - "Remove libdav1d from WebRTC (#8466)" - AV1 decoder removal (~533KB)
- **0cf29a87a777** - WebRTC subtree import with flag definitions

### Source Files

- `third_party/webrtc/media/engine/internal_encoder_factory.cc` - Original (not used in Starboard)
- `third_party/webrtc/media/engine/internal_encoder_factory_starboard_stub.cc` - Stub replacement
- `third_party/webrtc/media/engine/internal_decoder_factory.cc` - Conditional compilation example
- `third_party/blink/renderer/platform/peerconnection/video_codec_factory.cc` - Instantiation point
- `cobalt/shell/browser/shell.cc:843` - getUserMedia permission check

### Configuration Files

- `third_party/webrtc/webrtc.gni` - WebRTC build flags
- `third_party/webrtc/media/BUILD.gn:578` - Stub substitution logic
- `cobalt/build/configs/common.gn` - Cobalt codec configuration

### Documentation

- WebRTC Spec: https://www.w3.org/TR/webrtc/
- VP8 Spec: https://datatracker.ietf.org/doc/html/rfc6386
- VP9 Spec: https://storage.googleapis.com/downloads.webmproject.org/docs/vp9/vp9-bitstream-specification-v0.6-20160331-draft.pdf

### Bug Tracking

- Bug b/421171908 - Cobalt binary size optimization

---

## Appendix: Codec Comparison Table

| Codec | Year | Purpose | WebRTC Status | Cobalt Encoder | Cobalt Decoder |
|-------|------|---------|---------------|----------------|----------------|
| **VP8** | 2008 | Video conferencing | Mandatory (MTI) | ❌ Removed | ❌ Removed |
| **VP9** | 2013 | High-quality streaming | Optional | ❌ Removed | ❌ Removed |
| **H264** | 2003 | Universal compatibility | Optional | ❌ Not built | ⚠️ May be used |
| **AV1** | 2018 | Next-gen streaming | Optional | ❌ Not built | ❌ Removed |
| **Opus** | 2012 | Audio codec | Mandatory (MTI) | ✅ Used | ✅ Used |

---

## Appendix: Decision Timeline

```
┌─────────────────────────────────────────────────────────────┐
│ Timeline of Architectural Decisions                         │
└─────────────────────────────────────────────────────────────┘

2025-08-08  WebRTC imported with libvpx support
            │
            │ Problem identified:
            │ - Binary size too large (~700KB libvpx unused)
            │ - No video encoding in Cobalt
            │
2026-01-14  ├─→ Evaluated existing flags
            │    └─→ rtc_build_libvpx insufficient
            │
            ├─→ Considered approaches:
            │    1. Conditional compilation (rejected - messy)
            │    2. Upstream RTC_ENABLE_VP8 (future work)
            │    3. Runtime registry (too large)
            │    4. Stub replacement (CHOSEN ✓)
            │
            ├─→ Implemented stub
            │    - Clean separation
            │    - No upstream conflicts
            │    - Well-documented
            │
2026-01-14  ├─→ Commit 6226acf18d26 landed
            │    "Nolibvpx (#8468)"
            │    Binary size: -700KB
            │
2026-02-06  └─→ Architecture documentation added
                 └─→ This document created
```

---

## Appendix: FAQ

### Q: Why not just remove WebRTC entirely?

**A:** Cobalt uses WebRTC for:
- getUserMedia (audio capture for voice input)
- Audio processing infrastructure
- Future extensibility

Removing it entirely would require reimplementing audio capture.

### Q: Why keep the decoder conditional but stub the encoder?

**A:**
- Encoder uses templates (hard to conditionally compile)
- Encoder has NO codecs left for Starboard (all removed)
- Decoder uses if-statements (easy to conditionally compile)
- Decoder KEEPS H264/AV1 (may be used for video playback)

### Q: Is the stub ever executed?

**A:** Compiled ✅, Linked ✅, but execution:
- Tests: Maybe (if they create RTCPeerConnection)
- Production: No (Cobalt doesn't use RTCPeerConnection)
- Purpose: Defensive programming

### Q: What if we need encoding later?

**A:** Replace stub with platform-specific factory using hardware encoders:
- Android: MediaCodec
- iOS: VideoToolbox
- Linux: VA-API

### Q: Could this be upstreamed?

**A:** Possibly, but:
- WebRTC maintains VP8 as mandatory baseline
- Our stub is Starboard-specific
- Better path: propose RTC_ENABLE_VP8 define upstream

### Q: What about H264 encoder?

**A:** Also removed:
- `rtc_use_h264 = false` in `cobalt/build/configs/common.gn:63`
- H264 encoder has `WEBRTC_USE_H264` define (cleaner than VP8)
- Can be conditionally compiled if needed later

### Q: Performance impact?

**A:** None. The stub methods are never called in Cobalt production.

### Q: Security implications?

**A:** Positive. Reduced attack surface by removing ~900KB of unused codec code.

---

**Document End**

For questions or updates, contact the Cobalt team or refer to Bug b/421171908.
