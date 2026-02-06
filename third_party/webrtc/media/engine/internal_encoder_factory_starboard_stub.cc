// Copyright 2026 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// STARBOARD STUB IMPLEMENTATION FOR INTERNAL ENCODER FACTORY
//
// This file replaces internal_encoder_factory.cc for Starboard builds.
// See third_party/webrtc/media/BUILD.gn:578-581 for the substitution logic.
//
// WHY THIS STUB EXISTS:
// ---------------------
// Cobalt is a playback-only platform and does not support video encoding.
// We want to remove VP8/VP9 encoder dependencies to reduce binary size
// (~700KB for libvpx). However, the original internal_encoder_factory.cc
// uses unconditional includes and template parameters:
//
//   #include "...video_encoder_factory_template_libvpx_vp8_adapter.h"
//   #include "...video_encoder_factory_template_libvpx_vp9_adapter.h"
//
//   using Factory = VideoEncoderFactoryTemplate<
//       LibvpxVp8EncoderTemplateAdapter,  // Can't remove from template!
//       LibvpxVp9EncoderTemplateAdapter   // Can't remove from template!
//   >;
//
// Even with rtc_build_libvpx=false, these includes and template instantiations
// cause compilation to fail because the adapter headers transitively include
// VP8/VP9 codec headers which are removed from the build.
//
// WHY NOT USE CONDITIONAL COMPILATION (#ifdef)?
// ----------------------------------------------
// VP8 has no RTC_ENABLE_VP8 preprocessor define (unlike VP9, H264, AV1).
// VP8 was designed as the baseline mandatory codec for WebRTC, so upstream
// never added conditional compilation support. Adding #ifdef guards around
// template parameters with proper trailing comma handling would be messy:
//
//   VideoEncoderFactoryTemplate<
//   #if defined(RTC_ENABLE_VP8)
//       LibvpxVp8EncoderTemplateAdapter,
//   #endif
//   #if defined(RTC_ENABLE_VP9)
//       LibvpxVp9EncoderTemplateAdapter
//   #endif
//   >
//
// This is error-prone and creates maintenance burden across upstream updates.
//
// WHY NOT USE EXISTING GN FLAGS?
// -------------------------------
// rtc_build_libvpx only controls linking (deps += [rtc_libvpx_dir]), not
// compilation. The code still tries to #include the headers and instantiate
// the template parameters, leading to build failures.
//
// WHEN IS THIS STUB ACTUALLY CALLED?
// -----------------------------------
// This stub is instantiated as a member variable in:
//   third_party/blink/renderer/platform/peerconnection/video_codec_factory.cc
//   - EncoderAdapter::software_encoder_factory_
//
// It is called when:
//   1. Creating RTCPeerConnection (calls GetPcFactory() -> creates EncoderAdapter)
//   2. Querying RTP capabilities (e.g., RTCRtpSender.getCapabilities("video"))
//   3. WebRTC browser tests that instantiate PeerConnections
//
// In Cobalt production:
//   - RTCPeerConnection is NOT used (no peer-to-peer communication)
//   - MediaRecorder is NOT used (no video recording)
//   - Only getUserMedia(audio) is supported (no video encoding)
//
// Therefore, this stub is COMPILED and LINKED but RARELY/NEVER EXECUTED
// in production. It serves as defensive programming to prevent crashes if
// the encoder factory is ever accidentally accessed.
//
// COMPARISON WITH DECODER:
// ------------------------
// The decoder factory (internal_decoder_factory.cc) uses simple if-statements
// with conditional compilation, which is cleaner:
//
//   std::unique_ptr<VideoDecoder> InternalDecoderFactory::Create(...) {
//   #if !BUILDFLAG(IS_STARBOARD)
//     if (...VP8...) return CreateVp8Decoder();
//     if (...VP9...) return VP9Decoder::Create();
//   #endif
//     if (...H264...) return H264Decoder::Create();  // Keep H264 decoder
//   }
//
// Decoder doesn't use templates, so conditional compilation works well.
// Also, Cobalt needs H264 decoding for video playback, so we keep the
// decoder infrastructure and only remove VP8/VP9 selectively.
//
// ALTERNATIVES CONSIDERED:
// ------------------------
// 1. Add RTC_ENABLE_VP8 define upstream (would help, but requires upstream buy-in)
// 2. Use runtime codec registration instead of templates (large refactor)
// 3. Remove entire peerconnection module (breaks tests and future flexibility)
// 4. Use #ifdef with template specialization (complex, maintenance burden)
//
// The stub approach is the most pragmatic solution given current constraints.
//
// BINARY SIZE IMPACT:
// -------------------
// - This stub object file: ~7.7 KB
// - Original encoder factory: ~211 KB
// - Removed libvpx library: ~700 KB
// - Net savings: ~900 KB
//
// FUTURE WORK:
// ------------
// If Cobalt ever needs video encoding (e.g., MediaRecorder support), this
// stub would need to be replaced with a platform-specific encoder factory
// that uses hardware encoders (MediaCodec on Android, VideoToolbox on iOS).
//
// See: cobalt/libvpx_removal_architecture.md for detailed architectural analysis
//
// Bug: b/421171908

#include "media/engine/internal_encoder_factory.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "api/video_codecs/video_encoder_factory_template.h"

namespace webrtc {

std::vector<SdpVideoFormat> InternalEncoderFactory::GetSupportedFormats()
    const {
  // Starboard does not support video encoding.
  // Return empty list to indicate no codecs are available.
  return {};
}

std::unique_ptr<VideoEncoder> InternalEncoderFactory::Create(
    const Environment& env,
    const SdpVideoFormat& format) {
  // Starboard does not support video encoding.
  // Return nullptr to indicate encoder creation failed.
  return nullptr;
}

VideoEncoderFactory::CodecSupport InternalEncoderFactory::QueryCodecSupport(
    const SdpVideoFormat& format,
    std::optional<std::string> scalability_mode) const {
  // Starboard does not support video encoding.
  // Return unsupported for all codec queries.
  return VideoEncoderFactory::CodecSupport{.is_supported = false};
}

}  // namespace webrtc
