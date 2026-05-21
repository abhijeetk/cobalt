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

#include "starboard/shared/starboard/media/avc_util.h"

#include <string.h>

#include <type_traits>

#include "starboard/common/check_op.h"

namespace starboard {

namespace {

constexpr uint8_t kAnnexBHeader4[] = {0, 0, 0, 1};
constexpr uint8_t kAnnexBHeader3[] = {0, 0, 1};
// NALU types (lower 5 bits of the NALU header byte).
// H.264 spec: nal_unit_type = byte & 0x1F, nal_ref_idc = (byte >> 5) & 0x3.
// nal_ref_idc varies per encoder, so match on type only.
constexpr uint8_t kNaluTypeMask = 0x1F;
constexpr uint8_t kNaluTypeIdr = 5;  // IDR slice
constexpr uint8_t kNaluTypeSps = 7;  // Sequence Parameter Set
constexpr uint8_t kNaluTypePps = 8;  // Picture Parameter Set
constexpr uint8_t kNaluTypeAud = 9;  // Access Unit Delimiter

// Returns the start code size (3 or 4) if data begins with an AnnexB start
// code, or 0 if no start code is found.
size_t GetAnnexBStartCodeSize(const uint8_t* data, size_t size) {
  if (size >= 4 && memcmp(data, kAnnexBHeader4, 4) == 0) {
    return 4;
  }
  if (size >= 3 && memcmp(data, kAnnexBHeader3, 3) == 0) {
    return 3;
  }
  return 0;
}

// UInt8Type can be "uint8_t", or "const uint8_t".
template <typename UInt8Type>
bool AdvanceToNextAnnexBHeader(UInt8Type** annex_b_data,
                               size_t* annex_b_data_size) {
  SB_DCHECK(annex_b_data);
  SB_DCHECK(annex_b_data_size);

  // Skip past the current start code (3 or 4 bytes).
  size_t current_sc = GetAnnexBStartCodeSize(*annex_b_data, *annex_b_data_size);
  if (current_sc == 0) {
    return false;
  }

  *annex_b_data += current_sc;
  *annex_b_data_size -= current_sc;

  // Scan for the next start code (3 or 4 bytes).
  while (*annex_b_data_size > 0) {
    if (GetAnnexBStartCodeSize(*annex_b_data, *annex_b_data_size) > 0) {
      return true;
    }
    ++*annex_b_data;
    --*annex_b_data_size;
  }
  return true;
}

bool ExtractAnnexBNalu(const uint8_t** annex_b_data,
                       size_t* annex_b_data_size,
                       std::vector<uint8_t>* annex_b_nalu) {
  SB_DCHECK(annex_b_data);
  SB_DCHECK(annex_b_data_size);
  SB_DCHECK(annex_b_nalu);

  const uint8_t* saved_data = *annex_b_data;

  if (!AdvanceToNextAnnexBHeader(annex_b_data, annex_b_data_size)) {
    return false;
  }

  annex_b_nalu->assign(saved_data, *annex_b_data);

  return true;
}

}  // namespace

AvcParameterSets::AvcParameterSets(
    Format format,
    std::vector<std::vector<uint8_t>> parameter_sets,
    int first_sps_index,
    int first_pps_index,
    size_t combined_size_in_bytes,
    size_t combined_size_with_optionals_in_bytes)
    : format_(format),
      first_sps_index_(first_sps_index),
      first_pps_index_(first_pps_index),
      parameter_sets_(std::move(parameter_sets)),
      combined_size_in_bytes_(combined_size_in_bytes),
      combined_size_with_optionals_in_bytes_(
          combined_size_with_optionals_in_bytes) {}

// static
std::optional<AvcParameterSets> AvcParameterSets::CreateFromAnnexB(
    const uint8_t* data,
    size_t size) {
  if (size > 0 && GetAnnexBStartCodeSize(data, size) == 0) {
    return std::nullopt;
  }
  if (size == 0) {
    return AvcParameterSets(
        kAnnexB, /*parameter_sets=*/{}, /*first_sps_index=*/-1,
        /*first_pps_index=*/-1, /*combined_size_in_bytes=*/0,
        /*combined_size_with_optionals_in_bytes=*/0);
  }

  std::vector<std::vector<uint8_t>> parameter_sets;
  int first_sps_index = -1;
  int first_pps_index = -1;
  size_t combined_size_in_bytes = 0;
  size_t combined_size_with_optionals_in_bytes = 0;

  std::vector<uint8_t> nalu;
  while (size > 3 && ExtractAnnexBNalu(&data, &size, &nalu)) {
    // Find the NALU type byte after the start code (3 or 4 bytes).
    size_t sc_size = GetAnnexBStartCodeSize(nalu.data(), nalu.size());
    if (sc_size == 0 || nalu.size() <= sc_size) {
      continue;
    }
    uint8_t nalu_type = nalu[sc_size] & kNaluTypeMask;
    if (nalu_type == kNaluTypeSps) {
      if (first_sps_index == -1) {
        first_sps_index = static_cast<int>(parameter_sets.size());
      }
      combined_size_in_bytes += nalu.size();
      parameter_sets.push_back(std::move(nalu));
    } else if (nalu_type == kNaluTypePps) {
      if (first_pps_index == -1) {
        first_pps_index = static_cast<int>(parameter_sets.size());
      }
      combined_size_in_bytes += nalu.size();
      parameter_sets.push_back(std::move(nalu));
    } else if (nalu_type == kNaluTypeIdr) {
      break;
    } else if (nalu_type == kNaluTypeAud) {
      combined_size_with_optionals_in_bytes += nalu.size();
    }
  }

  return AvcParameterSets(kAnnexB, std::move(parameter_sets), first_sps_index,
                          first_pps_index, combined_size_in_bytes,
                          combined_size_with_optionals_in_bytes);
}

std::vector<uint8_t> AvcParameterSets::GetAllSpses() const {
  SB_DCHECK(format_ == kAnnexB);
  std::vector<uint8_t> spses;
  for (auto& parameter_set : parameter_sets_) {
    size_t sc =
        GetAnnexBStartCodeSize(parameter_set.data(), parameter_set.size());
    if ((parameter_set[sc] & kNaluTypeMask) == kNaluTypeSps) {
      spses.insert(spses.end(), parameter_set.begin(), parameter_set.end());
    }
  }
  return spses;
}

std::vector<uint8_t> AvcParameterSets::GetAllPpses() const {
  SB_DCHECK(format_ == kAnnexB);
  std::vector<uint8_t> ppses;
  for (auto& parameter_set : parameter_sets_) {
    size_t sc =
        GetAnnexBStartCodeSize(parameter_set.data(), parameter_set.size());
    if ((parameter_set[sc] & kNaluTypeMask) == kNaluTypePps) {
      ppses.insert(ppses.end(), parameter_set.begin(), parameter_set.end());
    }
  }
  return ppses;
}

AvcParameterSets AvcParameterSets::ConvertTo(Format new_format) const {
  if (format_ == new_format) {
    return *this;
  }

  SB_DCHECK(format_ == kAnnexB);
  SB_DCHECK(new_format == kHeadless);

  std::vector<std::vector<uint8_t>> new_parameter_sets = parameter_sets_;
  size_t new_combined_size_in_bytes = combined_size_in_bytes_;
  for (auto& parameter_set : new_parameter_sets) {
    size_t sc =
        GetAnnexBStartCodeSize(parameter_set.data(), parameter_set.size());
    parameter_set.erase(parameter_set.begin(), parameter_set.begin() + sc);
    new_combined_size_in_bytes -= sc;
  }

  return AvcParameterSets(new_format, std::move(new_parameter_sets),
                          first_sps_index_, first_pps_index_,
                          new_combined_size_in_bytes,
                          combined_size_with_optionals_in_bytes_);
}

bool AvcParameterSets::operator==(const AvcParameterSets& that) const {
  SB_DCHECK_EQ(format(), that.format());

  if (parameter_sets_ == that.parameter_sets_) {
    SB_DCHECK_EQ(first_sps_index_, that.first_sps_index_);
    SB_DCHECK_EQ(first_pps_index_, that.first_pps_index_);
    return true;
  }
  return false;
}

bool AvcParameterSets::operator!=(const AvcParameterSets& that) const {
  return !(*this == that);
}

bool ConvertAnnexBToAvcc(const uint8_t* annex_b_source,
                         size_t size,
                         uint8_t* avcc_destination) {
  if (size == 0) {
    return true;
  }

  SB_DCHECK(annex_b_source);
  SB_DCHECK(avcc_destination);

  size_t initial_sc = GetAnnexBStartCodeSize(annex_b_source, size);
  if (initial_sc == 0) {
    SB_LOG(ERROR)
        << "[ABHIJEET][HLS] ConvertAnnexBToAvcc: Not a valid AnnexB header.";
    return false;
  }

  auto annex_b_source_size = size;
  const uint8_t* last_source = annex_b_source;

  const auto kAvccLengthFieldSize = 4;

  while (AdvanceToNextAnnexBHeader(&annex_b_source, &annex_b_source_size)) {
    size_t nalu_total_size = annex_b_source - last_source;
    size_t current_sc_size =
        GetAnnexBStartCodeSize(last_source, nalu_total_size);
    SB_DCHECK(current_sc_size > 0);

    size_t payload_size = nalu_total_size - current_sc_size;

    // Write 4-byte length field.
    avcc_destination[0] =
        static_cast<uint8_t>((payload_size & 0xff000000) >> 24);
    avcc_destination[1] = static_cast<uint8_t>((payload_size & 0xff0000) >> 16);
    avcc_destination[2] = static_cast<uint8_t>((payload_size & 0xff00) >> 8);
    avcc_destination[3] = static_cast<uint8_t>(payload_size & 0xff);

    memcpy(avcc_destination + kAvccLengthFieldSize,
           last_source + current_sc_size, payload_size);

    avcc_destination += kAvccLengthFieldSize + payload_size;
    last_source = annex_b_source;
  }

  SB_DCHECK_EQ(annex_b_source_size, 0U);

  return true;
}

size_t GetAvccSizeFromAnnexB(const uint8_t* data, size_t size) {
  if (size == 0) {
    return 0;
  }

  size_t total_avcc_size = 0;
  const auto kAvccLengthFieldSize = 4;

  const uint8_t* last_pos = data;
  auto temp_data = data;
  auto temp_size = size;

  if (GetAnnexBStartCodeSize(temp_data, temp_size) == 0) {
    return 0;
  }

  while (AdvanceToNextAnnexBHeader(&temp_data, &temp_size)) {
    size_t nalu_total_size = temp_data - last_pos;
    size_t sc_size = GetAnnexBStartCodeSize(last_pos, nalu_total_size);
    SB_DCHECK(sc_size > 0);

    total_avcc_size += kAvccLengthFieldSize + (nalu_total_size - sc_size);
    last_pos = temp_data;
  }

  return total_avcc_size;
}

}  // namespace starboard
