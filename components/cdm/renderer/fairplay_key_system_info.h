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

#ifndef COMPONENTS_CDM_RENDERER_FAIRPLAY_KEY_SYSTEM_INFO_H_
#define COMPONENTS_CDM_RENDERER_FAIRPLAY_KEY_SYSTEM_INFO_H_

#include <string>

#include "base/containers/flat_set.h"
#include "media/base/content_decryption_module.h"
#include "media/base/eme_constants.h"
#include "media/base/key_system_info.h"

namespace cdm {

inline constexpr char kFairplayKeySystem[] = "com.youtube.fairplay";

// KeySystemInfo for Apple FairPlay Streaming.
class FairplayKeySystemInfo : public media::KeySystemInfo {
 public:
  FairplayKeySystemInfo(
      media::SupportedCodecs codecs,
      base::flat_set<media::EncryptionScheme> encryption_schemes,
      base::flat_set<media::CdmSessionType> session_types,
      media::SupportedCodecs hw_secure_codecs,
      base::flat_set<media::EncryptionScheme> hw_secure_encryption_schemes,
      base::flat_set<media::CdmSessionType> hw_secure_session_types,
      media::EmeFeatureSupport persistent_state_support,
      media::EmeFeatureSupport distinctive_identifier_support);
  ~FairplayKeySystemInfo() override;

  std::string GetBaseKeySystemName() const override;
  bool IsSupportedKeySystem(const std::string& key_system) const override;
  bool ShouldUseBaseKeySystemName() const override;
  bool IsSupportedInitDataType(
      media::EmeInitDataType init_data_type) const override;
  media::EmeConfig::Rule GetEncryptionSchemeConfigRule(
      media::EncryptionScheme encryption_scheme) const override;
  media::SupportedCodecs GetSupportedCodecs() const override;
  media::SupportedCodecs GetSupportedHwSecureCodecs() const override;
  media::EmeConfig::Rule GetRobustnessConfigRule(
      const std::string& key_system,
      media::EmeMediaType media_type,
      const std::string& requested_robustness,
      const bool* hw_secure_requirement) const override;
  media::EmeConfig::Rule GetPersistentLicenseSessionSupport() const override;
  media::EmeFeatureSupport GetPersistentStateSupport() const override;
  media::EmeFeatureSupport GetDistinctiveIdentifierSupport() const override;

 private:
  const media::SupportedCodecs codecs_;
  const base::flat_set<media::EncryptionScheme> encryption_schemes_;
  const base::flat_set<media::CdmSessionType> session_types_;
  const media::SupportedCodecs hw_secure_codecs_;
  const base::flat_set<media::EncryptionScheme> hw_secure_encryption_schemes_;
  const base::flat_set<media::CdmSessionType> hw_secure_session_types_;
  const media::EmeFeatureSupport persistent_state_support_;
  const media::EmeFeatureSupport distinctive_identifier_support_;
};

// TODO: Consolidate with FairplayKeySystemInfo once URL player path is retired.
// Only difference is the key system name for AVSBDL routing.
inline constexpr char kFairplayKeySystemSbdl[] = "com.youtube.fairplay.sbdl";

class FairplayKeySystemInfoSBDL : public FairplayKeySystemInfo {
 public:
  using FairplayKeySystemInfo::FairplayKeySystemInfo;
  std::string GetBaseKeySystemName() const override;
  bool IsSupportedKeySystem(const std::string& key_system) const override;
};

}  // namespace cdm

#endif  // COMPONENTS_CDM_RENDERER_FAIRPLAY_KEY_SYSTEM_INFO_H_
