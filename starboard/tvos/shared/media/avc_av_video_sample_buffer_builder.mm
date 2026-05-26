// Copyright 2020 The Cobalt Authors. All Rights Reserved.
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

#import "starboard/tvos/shared/media/avc_av_video_sample_buffer_builder.h"

#include <iomanip>

#import "starboard/tvos/shared/media/playback_capabilities.h"

namespace starboard {

namespace {

size_t GetAnnexBStartCodeSize(const uint8_t* data, size_t size) {
  if (size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 &&
      data[3] == 1) {
    return 4;
  }
  if (size >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1) {
    return 3;
  }
  return 0;
}

const uint8_t* FindNextStartCode(const uint8_t* data, size_t size) {
  for (size_t i = 0; i + 3 <= size; i++) {
    if (GetAnnexBStartCodeSize(data + i, size - i) > 0) {
      return data + i;
    }
  }
  return nullptr;
}

}  // namespace

AvcAVVideoSampleBufferBuilder::~AvcAVVideoSampleBufferBuilder() {
  Reset();
}

int64_t AvcAVVideoSampleBufferBuilder::DecodingTimeNeededPerFrame() const {
  if (PlaybackCapabilities::IsAppleTVHD()) {
    return 10000;  // 10ms
  }
  return 5000;  // 5ms
}

void AvcAVVideoSampleBufferBuilder::Reset() {
  SB_DCHECK(thread_checker_.CalledOnValidThread());
  AVVideoSampleBufferBuilder::Reset();
  video_config_ = std::nullopt;
  if (format_description_) {
    CFRelease(format_description_);
    format_description_ = nullptr;
  }
}

void AvcAVVideoSampleBufferBuilder::WriteInputBuffer(
    const scoped_refptr<InputBuffer>& input_buffer,
    int64_t media_time_offset) {
  SB_DCHECK(thread_checker_.CalledOnValidThread());
  SB_DCHECK(input_buffer);
  SB_DCHECK(!error_occurred_);
  SB_DCHECK(output_cb_);
  SB_DCHECK(input_buffer->video_stream_info().codec == kSbMediaVideoCodecH264);

  const auto& sample_info = input_buffer->video_sample_info();

  SB_LOG(INFO) << "[ABHIJEET][HLS] WriteInputBuffer:"
               << " frame=" << frame_counter_
               << " is_key=" << sample_info.is_key_frame
               << " size=" << input_buffer->size();
  if (frame_counter_ == 0 && !sample_info.is_key_frame) {
    ReportError("The first frame should be key frame.");
    return;
  }

  const uint8_t* full_data = input_buffer->data();
  size_t full_size = input_buffer->size();
  const uint8_t* source_data = full_data;
  size_t data_size = full_size;
  size_t bytes_to_skip = 0;

  if (sample_info.is_key_frame) {
    auto new_config =
        VideoConfig::Create(input_buffer->video_stream_info(),
                            input_buffer->data(), input_buffer->size());
    if (!new_config) {
      ReportError("Failed to parse video config.");
      return;
    }
    const auto& parameter_sets = new_config->avc_parameter_sets();
    if (!video_config_ || video_config_.value() != *new_config) {
      video_config_.emplace(*new_config);
      if (!RefreshAVCFormatDescription(parameter_sets)) {
        return;
      }
    }
    SB_DCHECK(parameter_sets.format() == AvcParameterSets::kAnnexB);
    bytes_to_skip = parameter_sets.combined_size_in_bytes_with_optionals();
    SB_LOG(INFO) << "[ABHIJEET][HLS] Key frame: bytes_to_skip=" << bytes_to_skip
                 << " data_size=" << data_size
                 << " remaining=" << (data_size - bytes_to_skip);
    if (bytes_to_skip > data_size) {
      ReportError("Invalid parameter set size exceeds buffer size.");
      return;
    }
    source_data += bytes_to_skip;
    data_size -= bytes_to_skip;
  }

  // Adjust subsample mapping for FairPlay
  const auto* drm_info = input_buffer->drm_info();
  std::optional<std::vector<SbDrmSubSampleMapping>> adjusted_mapping;

  if (drm_info) {
    std::vector<SbDrmSubSampleMapping> mapping(
        drm_info->subsample_mapping,
        drm_info->subsample_mapping + drm_info->subsample_count);

    std::stringstream orig_log;
    orig_log << "Original mapping [" << drm_info->subsample_count << "]: ";
    for (int i = 0; i < std::min(drm_info->subsample_count, 5); ++i) {
      orig_log << "{" << mapping[i].clear_byte_count << ","
               << mapping[i].encrypted_byte_count << "} ";
    }
    SB_LOG(INFO) << "[ABHIJEET][DRM] " << orig_log.str();

    // 1. Adjust for bytes_to_skip (AUD, SPS, PPS stripped from Annex-B)
    size_t to_skip = bytes_to_skip;
    size_t first_remaining_idx = 0;
    while (to_skip > 0 && first_remaining_idx < mapping.size()) {
      uint32_t sub_total = mapping[first_remaining_idx].clear_byte_count +
                           mapping[first_remaining_idx].encrypted_byte_count;
      if (to_skip >= sub_total) {
        to_skip -= sub_total;
        first_remaining_idx++;
      } else {
        mapping[first_remaining_idx].clear_byte_count -= to_skip;
        to_skip = 0;
      }
    }

    // 2. Adjust for AVCC growth (3-byte -> 4-byte start codes)
    // We iterate through NALUs in the REMAINING source data.
    const uint8_t* nalu_ptr = source_data;
    size_t nalu_size_remaining = data_size;
    size_t mapping_idx = first_remaining_idx;

    while (nalu_size_remaining > 0 && mapping_idx < mapping.size()) {
      size_t sc_size = GetAnnexBStartCodeSize(nalu_ptr, nalu_size_remaining);
      if (sc_size == 0) {
        break;
      }

      // If Annex-B start code was 3 bytes, it grows to 4 bytes in AVCC.
      if (sc_size == 3) {
        mapping[mapping_idx].clear_byte_count += 1;
      }

      // Find next start code to determine NALU total size
      const uint8_t* next_sc =
          FindNextStartCode(nalu_ptr + sc_size, nalu_size_remaining - sc_size);
      size_t nalu_total = next_sc ? (next_sc - nalu_ptr) : nalu_size_remaining;

      nalu_ptr += nalu_total;
      nalu_size_remaining -= nalu_total;
      mapping_idx++;
    }

    if (first_remaining_idx < mapping.size()) {
      std::vector<SbDrmSubSampleMapping> final_mapping(
          mapping.begin() + first_remaining_idx, mapping.end());

      size_t mapping_total = 0;
      std::stringstream adj_log;
      adj_log << "Adjusted mapping [" << final_mapping.size() << "]: ";
      for (size_t i = 0; i < final_mapping.size(); ++i) {
        mapping_total += final_mapping[i].clear_byte_count +
                         final_mapping[i].encrypted_byte_count;
        if (i < 5) {
          adj_log << "{" << final_mapping[i].clear_byte_count << ","
                  << final_mapping[i].encrypted_byte_count << "} ";
        }
      }
      SB_LOG(INFO) << "[ABHIJEET][DRM] " << adj_log.str()
                   << " Total size=" << mapping_total;

      adjusted_mapping = std::move(final_mapping);
    }
  }

  size_t avcc_size = GetAvccSizeFromAnnexB(source_data, data_size);
  bool is_annex_b = (avcc_size > 0);
  if (!is_annex_b) {
    avcc_size = data_size;
  }

  if (adjusted_mapping.has_value()) {
    size_t mapping_total = 0;
    for (const auto& m : *adjusted_mapping) {
      mapping_total += m.clear_byte_count + m.encrypted_byte_count;
    }
    if (mapping_total != avcc_size) {
      SB_LOG(ERROR) << "[ABHIJEET][DRM] MAPPING SIZE MISMATCH: mapping="
                    << mapping_total << " avcc_size=" << avcc_size;
    }
  }

  CMBlockBufferRef block;
  OSStatus status = CMBlockBufferCreateWithMemoryBlock(
      NULL, NULL, avcc_size, NULL, NULL, 0, avcc_size,
      kCMBlockBufferAssureMemoryNowFlag, &block);
  if (status != 0) {
    ReportOSError("BlockBufferCreate", status);
    return;
  }

  char* block_data;
  size_t total_block_size = 0;
  status = CMBlockBufferGetDataPointer(block, 0, nullptr, &total_block_size,
                                       &block_data);
  if (status != 0) {
    ReportOSError("BlockGetDataPointer", status);
    CFRelease(block);
    return;
  }
  SB_DCHECK(total_block_size == avcc_size);

  if (is_annex_b) {
    if (!ConvertAnnexBToAvcc(source_data, data_size,
                             reinterpret_cast<uint8_t*>(block_data))) {
      ReportError("Failed to convert input data into avcc format");
      CFRelease(block);
      return;
    }
  } else {
    memcpy(block_data, source_data, data_size);
  }

  CMSampleTimingInfo timing_info;
  timing_info.decodeTimeStamp = CMTimeMake(frame_counter_++, 1);
  timing_info.presentationTimeStamp =
      CMTimeMake(input_buffer->timestamp() + media_time_offset, 1000000);
  timing_info.duration = kCMTimeInvalid;

  // Log format description and sample data for encrypted content debugging.
  if (drm_info) {
    SB_LOG(INFO) << "[ABHIJEET][DRM] VideoSampleBuffer: keyframe="
                 << sample_info.is_key_frame << " avcc_size=" << avcc_size
                 << " is_annex_b=" << is_annex_b
                 << " pts_us=" << input_buffer->timestamp()
                 << " offset=" << media_time_offset
                 << " subsample_count=" << drm_info->subsample_count
                 << " format_desc="
                 << (format_description_ ? "present" : "NULL");
    if (format_description_) {
      CMVideoDimensions dims =
          CMVideoFormatDescriptionGetDimensions(format_description_);
      SB_LOG(INFO) << "[ABHIJEET][DRM] VideoSampleBuffer format: " << dims.width
                   << "x" << dims.height << " codec="
                   << CMFormatDescriptionGetMediaSubType(format_description_);
    }
  }

  CMSampleBufferRef cm_sample_buffer;
  status = CMSampleBufferCreateReady(kCFAllocatorDefault, block,
                                     format_description_, 1, 1, &timing_info, 1,
                                     &avcc_size, &cm_sample_buffer);
  CFRelease(block);
  if (status != 0) {
    ReportOSError("SampleBufferCreate", status);
    return;
  }

  scoped_refptr<AVSampleBuffer> sample_buffer(new AVSampleBuffer(
      cm_sample_buffer, input_buffer, std::move(adjusted_mapping)));
  output_cb_(sample_buffer);
}

bool AvcAVVideoSampleBufferBuilder::RefreshAVCFormatDescription(
    const AvcParameterSets& parameter_sets) {
  SB_DCHECK(parameter_sets.format() == AvcParameterSets::kAnnexB);

  if (format_description_) {
    CFRelease(format_description_);
  }
  auto parameter_sets_headless =
      parameter_sets.ConvertTo(AvcParameterSets::kHeadless);

  auto parameter_set_addresses = parameter_sets_headless.GetAddresses();
  auto parameter_set_sizes = parameter_sets_headless.GetSizesInBytes();
  SB_DCHECK(parameter_set_addresses.size() == parameter_set_sizes.size());

  const size_t kAvccLengthInBytes = 4;
  OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
      kCFAllocatorDefault, parameter_set_addresses.size(),
      parameter_set_addresses.data(), parameter_set_sizes.data(),
      kAvccLengthInBytes, &format_description_);
  if (status != 0) {
    ReportOSError("RefreshAVCFormatDescription", status);
    return false;
  }
  return true;
}

}  // namespace starboard
