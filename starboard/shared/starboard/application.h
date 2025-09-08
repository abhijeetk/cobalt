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

#ifndef STARBOARD_SHARED_STARBOARD_APPLICATION_H_
#define STARBOARD_SHARED_STARBOARD_APPLICATION_H_

#include <memory>

#include "starboard/common/command_line.h"
#include "starboard/common/ref_counted.h"
#include "starboard/event.h"
#include "starboard/player.h"
#include "starboard/shared/internal_only.h"
#include "starboard/shared/starboard/player/filter/video_frame_internal.h"
#include "starboard/types.h"

namespace starboard {
namespace shared {
namespace starboard {

class SB_EXPORT_ANDROID Application {
 public:
  typedef player::filter::VideoFrame VideoFrame;
  typedef SbEventDataDestructor EventHandledCallback;

  explicit Application(SbEventHandleCallback sb_event_handle_callback);
  virtual ~Application();

  static Application* Get();

  virtual int Run(CommandLine command_line, const char* link_data) = 0;
  virtual int Run(CommandLine command_line) = 0;
  virtual int Run(int argc, char** argv, const char* link_data) = 0;
  virtual int Run(int argc, char** argv) = 0;

  const CommandLine* GetCommandLine();

  virtual void Blur(void* context, EventHandledCallback callback) = 0;
  virtual void Focus(void* context, EventHandledCallback callback) = 0;
  virtual void Conceal(void* context, EventHandledCallback callback) = 0;
  virtual void Reveal(void* context, EventHandledCallback callback) = 0;
  virtual void Freeze(void* context, EventHandledCallback callback) = 0;
  virtual void Unfreeze(void* context, EventHandledCallback callback) = 0;
  virtual void Stop(int error_level) = 0;
  virtual void Link(const char* link_data) = 0;
  virtual void InjectLowMemoryEvent() = 0;
  virtual void InjectOsNetworkDisconnectedEvent() = 0;
  virtual void InjectOsNetworkConnectedEvent() = 0;
  virtual void WindowSizeChanged(void* context, EventHandledCallback callback) = 0;

  virtual SbEventId Schedule(SbEventCallback callback,
                             void* context,
                             int64_t delay) = 0;
  virtual void Cancel(SbEventId id) = 0;

  virtual void HandleFrame(SbPlayer player,
                           const scoped_refptr<VideoFrame>& frame,
                           int z_index,
                           int x,
                           int y,
                           int width,
                           int height) = 0;

 protected:
  void SetCommandLine(int argc, const char** argv) {
    command_line_.reset(new CommandLine(argc, argv));
  }

  void SetCommandLine(std::unique_ptr<CommandLine> command_line) {
    command_line_ = std::move(command_line);
  }

  std::unique_ptr<CommandLine> command_line_;
  SbEventHandleCallback sb_event_handle_callback_;
};

}  // namespace starboard
}  // namespace shared
}  // namespace starboard

#endif  // STARBOARD_SHARED_STARBOARD_APPLICATION_H_