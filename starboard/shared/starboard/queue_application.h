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

#ifndef STARBOARD_SHARED_STARBOARD_QUEUE_APPLICATION_H_
#define STARBOARD_SHARED_STARBOARD_QUEUE_APPLICATION_H_

#include <pthread.h>

#include <map>
#include <mutex>
#include <set>
#include <vector>

#include "starboard/common/queue.h"
#include "starboard/common/time.h"
#include "starboard/shared/internal_only.h"
#include "starboard/shared/starboard/application.h"
#include "starboard/types.h"

namespace starboard {
namespace shared {
namespace starboard {

class QueueApplication : public Application {
 public:
  explicit QueueApplication(SbEventHandleCallback sb_event_handle_callback);
  ~QueueApplication() override;

  // --- Application Overrides ---
  int Run(CommandLine command_line, const char* link_data) override;
  int Run(CommandLine command_line) override;
  int Run(int argc, char** argv, const char* link_data) override;
  int Run(int argc, char** argv) override;

  void Blur(void* context, EventHandledCallback callback) override;
  void Focus(void* context, EventHandledCallback callback) override;
  void Conceal(void* context, EventHandledCallback callback) override;
  void Reveal(void* context, EventHandledCallback callback) override;
  void Freeze(void* context, EventHandledCallback callback) override;
  void Unfreeze(void* context, EventHandledCallback callback) override;
  void Stop(int error_level) override;
  void Link(const char* link_data) override;
  void InjectLowMemoryEvent() override;
  void InjectOsNetworkDisconnectedEvent() override;
  void InjectOsNetworkConnectedEvent() override;
  void WindowSizeChanged(void* context, EventHandledCallback callback) override;

  SbEventId Schedule(SbEventCallback callback,
                     void* context,
                     int64_t delay) override;
  void Cancel(SbEventId id) override;

  void HandleFrame(SbPlayer player,
                   const scoped_refptr<VideoFrame>& frame,
                   int z_index,
                   int x,
                   int y,
                   int width,
                   int height) override;

 protected:
  // --- Event and Lifecycle Structs ---
  enum State {
    kStateUnstarted,
    kStateStarted,
    kStateBlurred,
    kStateConcealed,
    kStateFrozen,
    kStateStopped,
  };

  struct TimedEvent;
  struct Event;

  // --- Platform-specific event handling --- 
  virtual bool MayHaveSystemEvents() = 0;
  virtual Event* PollNextSystemEvent() = 0;
  virtual Event* WaitForSystemEventWithTimeout(int64_t time) = 0;
  virtual void WakeSystemEventWait() = 0;

 private:
  // --- Private helper methods ---
  int RunLoop();
  void DispatchNextEvent();
  bool DispatchAndDelete(Event* event);
  bool HandleEventAndUpdateState(Event* event);
  Event* CreateInitialEvent(SbEventType type, int64_t timestamp);
  void DispatchStart(int64_t timestamp);
  void DispatchPreload(int64_t timestamp);
  bool IsStartImmediate();
  bool IsPreloadImmediate();
  bool HasPreloadSwitch();
  void CallTeardownCallbacks();
  void SetStartLink(const char* start_link);

  Event* GetNextEvent();
  void Inject(Event* event);
  void InjectTimedEvent(TimedEvent* timed_event);
  void CancelTimedEvent(SbEventId event_id);
  TimedEvent* GetNextDueTimedEvent();
  int64_t GetNextTimedEventTargetTime();

  // --- Private members ---
  pthread_t thread_;
  State state_;
  int error_level_;
  char* start_link_;
  std::mutex callbacks_lock_;
  std::vector<TeardownCallback> teardown_callbacks_;

  class EventQueue;
  class TimedEventQueue;

  std::unique_ptr<EventQueue> event_queue_;
  std::unique_ptr<TimedEventQueue> timed_event_queue_;
};

}  // namespace starboard
}  // namespace shared
}  // namespace starboard

#endif  // STARBOARD_SHARED_STARBOARD_QUEUE_APPLICATION_H_