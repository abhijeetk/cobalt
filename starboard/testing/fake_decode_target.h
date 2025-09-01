// Copyright 2025 The Cobalt Authors. All Rights Reserved.
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

// ENHANCED: Created for DecodeTargetProvider testing integration
// Borrowed concepts from FakeGraphicsContextProvider and enhanced for DTT mode

#ifndef STARBOARD_TESTING_FAKE_DECODE_TARGET_H_
#define STARBOARD_TESTING_FAKE_DECODE_TARGET_H_

#include "base/logging.h"
#include "base/threading/platform_thread.h"
#include "starboard/decode_target.h"
#include "starboard/gles.h"

namespace starboard {
namespace testing {

// Enhanced fake decode target that simulates decode-to-texture functionality
// Borrowed from FakeGraphicsContextProvider patterns and enhanced with DTT logging
class FakeDecodeTarget {
 public:
  FakeDecodeTarget() {
    // ENHANCED: Create a fake GL texture ID to simulate decode target
    fake_texture_id_ = 12345;  // Fake texture ID for testing
    
    LOG(INFO) << "[DTT-DEBUG] FakeDecodeTarget created with texture ID: " << fake_texture_id_
              << " (Thread: " << base::PlatformThread::CurrentId() << ")";
  }

  ~FakeDecodeTarget() {
    LOG(INFO) << "[DTT-DEBUG] FakeDecodeTarget destroyed, texture ID: " << fake_texture_id_
              << " (Thread: " << base::PlatformThread::CurrentId() << ")";
  }

  // Create a fake SbDecodeTarget for testing
  // ENHANCED: Borrowed from Android decode target patterns but simplified for testing
  static SbDecodeTarget CreateFakeSbDecodeTarget() {
    LOG(INFO) << "[DTT-DEBUG] CreateFakeSbDecodeTarget called"
              << " (Thread: " << base::PlatformThread::CurrentId() << ")";
    
    // Create a fake decode target pointer - in real implementation this would be
    // created by the platform-specific video decoder
    FakeDecodeTarget* fake_target = new FakeDecodeTarget();
    
    LOG(INFO) << "[DTT-DEBUG] Created fake SbDecodeTarget: " << fake_target
              << " (Thread: " << base::PlatformThread::CurrentId() << ")";
              
    return reinterpret_cast<SbDecodeTarget>(fake_target);
  }

  // Release a fake SbDecodeTarget
  // ENHANCED: Proper cleanup with logging
  static void ReleaseFakeSbDecodeTarget(SbDecodeTarget decode_target) {
    if (decode_target == kSbDecodeTargetInvalid) {
      LOG(WARNING) << "[DTT-DEBUG] ReleaseFakeSbDecodeTarget: Invalid target"
                   << " (Thread: " << base::PlatformThread::CurrentId() << ")";
      return;
    }
    
    LOG(INFO) << "[DTT-DEBUG] ReleaseFakeSbDecodeTarget: " << decode_target
              << " (Thread: " << base::PlatformThread::CurrentId() << ")";
              
    FakeDecodeTarget* fake_target = reinterpret_cast<FakeDecodeTarget*>(decode_target);
    delete fake_target;
  }

  // Get info from fake decode target
  // ENHANCED: Borrowed from real decode target info patterns
  static bool GetFakeDecodeTargetInfo(SbDecodeTarget decode_target, SbDecodeTargetInfo* out_info) {
    if (decode_target == kSbDecodeTargetInvalid || !out_info) {
      LOG(WARNING) << "[DTT-DEBUG] GetFakeDecodeTargetInfo: Invalid parameters"
                   << " (Thread: " << base::PlatformThread::CurrentId() << ")";
      return false;
    }
    
    FakeDecodeTarget* fake_target = reinterpret_cast<FakeDecodeTarget*>(decode_target);
    
    LOG(INFO) << "[DTT-DEBUG] GetFakeDecodeTargetInfo for target: " << decode_target
              << " texture: " << fake_target->fake_texture_id_
              << " (Thread: " << base::PlatformThread::CurrentId() << ")";
    
    // Fill in fake decode target info
    // ENHANCED: Simulate a typical decode-to-texture setup
    out_info->format = kSbDecodeTargetFormat1PlaneRGBA;
    out_info->is_opaque = true;
    out_info->width = 1920;  // Fake 1080p resolution
    out_info->height = 1080;
    
    // Simulate single plane RGBA texture
    out_info->planes[kSbDecodeTargetPlaneRGBA].texture = fake_target->fake_texture_id_;
    out_info->planes[kSbDecodeTargetPlaneRGBA].gl_texture_target = SB_GL_TEXTURE_2D;
    out_info->planes[kSbDecodeTargetPlaneRGBA].width = out_info->width;
    out_info->planes[kSbDecodeTargetPlaneRGBA].height = out_info->height;
    out_info->planes[kSbDecodeTargetPlaneRGBA].content_region.left = 0.0f;
    out_info->planes[kSbDecodeTargetPlaneRGBA].content_region.top = 0.0f;
    out_info->planes[kSbDecodeTargetPlaneRGBA].content_region.right = 1.0f;
    out_info->planes[kSbDecodeTargetPlaneRGBA].content_region.bottom = 1.0f;
    
    return true;
  }

  uint32_t texture_id() const { return fake_texture_id_; }

 private:
  uint32_t fake_texture_id_;
};

}  // namespace testing
}  // namespace starboard

#endif  // STARBOARD_TESTING_FAKE_DECODE_TARGET_H_