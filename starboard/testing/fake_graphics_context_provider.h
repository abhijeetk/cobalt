// Copyright 2018 The Cobalt Authors. All Rights Reserved.
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

#ifndef STARBOARD_TESTING_FAKE_GRAPHICS_CONTEXT_PROVIDER_H_
#define STARBOARD_TESTING_FAKE_GRAPHICS_CONTEXT_PROVIDER_H_

#include <pthread.h>

#include <functional>

#include "starboard/common/queue.h"
#include "starboard/configuration.h"
#include "starboard/decode_target.h"
#include "starboard/window.h"

#include "starboard/egl.h"
#include "starboard/gles.h"

// ENHANCED: Added includes for DTT testing borrowed from origin/25.lts.stable patterns
#include "base/logging.h"
#include "base/threading/platform_thread.h"
#include "base/command_line.h"
#include "base/synchronization/lock.h"
#include "content/public/common/content_switches.h"

namespace starboard {
namespace testing {

// This class provides a SbDecodeTargetGraphicsContextProvider implementation
// used by SbPlayer related tests.  It creates a thread and forwards decode
// target creation/destruction to the thread.
class FakeGraphicsContextProvider {
 public:
  FakeGraphicsContextProvider();
  ~FakeGraphicsContextProvider();

  SbWindow window() { return kSbWindowInvalid; }
  SbDecodeTargetGraphicsContextProvider* decoder_target_provider() {
    return &decoder_target_provider_;
  }

  void RunOnGlesContextThread(const std::function<void()>& functor);
  void ReleaseDecodeTarget(SbDecodeTarget decode_target);

  void Render();

  // ENHANCED: Added DecodeTargetProvider testing support
  // Borrowed from origin/25.lts.stable DecodeTargetProvider patterns
  SbDecodeTarget CreateFakeDecodeTarget();
  bool GetFakeDecodeTargetInfo(SbDecodeTarget decode_target, SbDecodeTargetInfo* out_info);
  void ReleaseFakeDecodeTarget(SbDecodeTarget decode_target);

  // Support for DecodeTargetProvider GetCurrentSbDecodeTarget function
  SbDecodeTarget GetCurrentDecodeTarget();
  void SetCurrentDecodeTarget(SbDecodeTarget decode_target);

  // Enhanced logging support
  void LogProcessAndThreadInfo(const std::string& operation) const;

 private:
  static void* ThreadEntryPoint(void* context);
  void RunLoop();

  void InitializeEGL();

  void OnDecodeTargetGlesContextRunner(
      SbDecodeTargetGlesContextRunnerTarget target_function,
      void* target_function_context);

  void MakeContextCurrent();
  void MakeNoContextCurrent();
  void DestroyContext();

  static void DecodeTargetGlesContextRunner(
      SbDecodeTargetGraphicsContextProvider* graphics_context_provider,
      SbDecodeTargetGlesContextRunnerTarget target_function,
      void* target_function_context);

  SbEglDisplay display_;
  SbEglSurface surface_;
  SbEglContext context_;
  Queue<std::function<void()>> functor_queue_;
  pthread_t decode_target_context_thread_;

  SbDecodeTargetGraphicsContextProvider decoder_target_provider_;

  // ENHANCED: Added fake decode target management
  // Borrowed from FakeDecodeTarget and DecodeTargetProvider patterns
  mutable SbDecodeTarget current_fake_decode_target_;
  mutable uint32_t fake_texture_counter_;
  mutable base::Lock fake_decode_target_lock_;
};

}  // namespace testing
}  // namespace starboard

#endif  // STARBOARD_TESTING_FAKE_GRAPHICS_CONTEXT_PROVIDER_H_
