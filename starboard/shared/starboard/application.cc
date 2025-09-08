// Copyright 2015 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/shared/starboard/application.h"

#include <atomic>

#include "starboard/common/check_op.h"

namespace starboard {
namespace shared {
namespace starboard {

namespace {

// The single application instance.
std::atomic<Application*> g_instance = {nullptr};

}  // namespace

Application::Application(SbEventHandleCallback sb_event_handle_callback)
    : sb_event_handle_callback_(sb_event_handle_callback) {
  SB_CHECK(sb_event_handle_callback_)
      << "sb_event_handle_callback_ has not been set.";
  Application* expected = nullptr;
  SB_CHECK(g_instance.compare_exchange_strong(expected,
                                              /*desired=*/this,
                                              std::memory_order_acq_rel));
}

Application::~Application() {
  Application* expected = this;
  SB_CHECK(g_instance.compare_exchange_strong(expected, /*desired=*/nullptr,
                                              std::memory_order_acq_rel));
}

Application* Application::Get() {
  Application* instance = g_instance.load(std::memory_order_acquire);
  SB_CHECK(instance);
  return instance;
}

const CommandLine* Application::GetCommandLine() {
  return command_line_.get();
}

}  // namespace starboard
}  // namespace shared
}  // namespace starboard