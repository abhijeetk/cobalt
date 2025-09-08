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

#include "starboard/shared/starboard/queue_application.h"

#include <atomic>
#include <limits>
#include <string>

#include "starboard/common/check_op.h"
#include "starboard/common/command_line.h"
#include "starboard/common/log.h"
#include "starboard/common/string.h"
#include "starboard/configuration.h"
#include "starboard/event.h"

namespace starboard {
namespace shared {
namespace starboard {

namespace {

const char kPreloadSwitch[] = "preload";
const char kLinkSwitch[] = "link";
const char kMinLogLevel[] = "min_log_level";
// Chromium's base/base_switches.h "--v". Positive numbers are equivalent to
// debug (0), info, warning, error, fatal. Note that Starboard has no debug;
// levels start at kSbLogPriorityInfo which is a 1.
const char kV[] = "v";

void DeleteStartData(void* data) {
  SbEventStartData* start_data = static_cast<SbEventStartData*>(data);
  if (start_data) {
    delete[] start_data->argument_values;
  }
  delete start_data;
}

// The next event ID to use for Schedule().
volatile std::atomic<int32_t> g_next_event_id{0};

}  // namespace

QueueApplication::QueueApplication(SbEventHandleCallback sb_event_handle_callback)
    : Application(sb_event_handle_callback),
      error_level_(0),
      thread_(pthread_self()),
      start_link_(nullptr),
      state_(kStateUnstarted) {
  event_queue_ = std::make_unique<EventQueue>();
  timed_event_queue_ = std::make_unique<TimedEventQueue>();
}

QueueApplication::~QueueApplication() {
  free(start_link_);
}

int QueueApplication::Run(CommandLine command_line, const char* link_data) {
  Initialize();
  SetCommandLine(std::make_unique<CommandLine>(command_line));
  if (link_data) {
    SetStartLink(link_data);
  }

  return RunLoop();
}

int QueueApplication::Run(CommandLine command_line) {
  Initialize();
  SetCommandLine(std::make_unique<CommandLine>(command_line));

  if (GetCommandLine()->HasSwitch(kLinkSwitch)) {
    std::string value = GetCommandLine()->GetSwitchValue(kLinkSwitch);
    if (!value.empty()) {
      SetStartLink(value.c_str());
    }
  }

  // kMinLogLevel should take priority over kV if both are defined.
  if (GetCommandLine()->HasSwitch(kMinLogLevel)) {
    ::starboard::logging::SetMinLogLevel(::starboard::logging::StringToLogLevel(
        GetCommandLine()->GetSwitchValue(kMinLogLevel)));
  } else if (GetCommandLine()->HasSwitch(kV)) {
    ::starboard::logging::SetMinLogLevel(
        ::starboard::logging::ChromiumIntToStarboardLogLevel(
            GetCommandLine()->GetSwitchValue(kV)));
  } else {
#if SB_LOGGING_IS_OFFICIAL_BUILD
    ::starboard::logging::SetMinLogLevel(::starboard::logging::SB_LOG_FATAL);
#else
    ::starboard::logging::SetMinLogLevel(::starboard::logging::SB_LOG_INFO);
#endif
  }

  return RunLoop();
}

int QueueApplication::Run(int argc, char** argv, const char* link_data) {
  return Run(CommandLine(argc, argv), link_data);
}

int QueueApplication::Run(int argc, char** argv) {
  return Run(CommandLine(argc, argv));
}

void QueueApplication::Blur(void* context, EventHandledCallback callback) {
  Inject(new Event(kSbEventTypeBlur, context, callback));
}

void QueueApplication::Focus(void* context, EventHandledCallback callback) {
  Inject(new Event(kSbEventTypeFocus, context, callback));
}

void QueueApplication::Conceal(void* context, EventHandledCallback callback) {
  Inject(new Event(kSbEventTypeConceal, context, callback));
}

void QueueApplication::Reveal(void* context, EventHandledCallback callback) {
  Inject(new Event(kSbEventTypeReveal, context, callback));
}

void QueueApplication::Freeze(void* context, EventHandledCallback callback) {
  Inject(new Event(kSbEventTypeFreeze, context, callback));
}

void QueueApplication::Unfreeze(void* context, EventHandledCallback callback) {
  Inject(new Event(kSbEventTypeUnfreeze, context, callback));
}

void QueueApplication::Stop(int error_level) {
  Event* event = new Event(kSbEventTypeStop, NULL, NULL);
  event->error_level = error_level;
  Inject(event);
}

void QueueApplication::Link(const char* link_data) {
  SB_DCHECK(link_data) << "You must call Link with link_data.";
  Inject(new Event(kSbEventTypeLink, strdup(link_data), free));
}

void QueueApplication::InjectLowMemoryEvent() {
  Inject(new Event(kSbEventTypeLowMemory, NULL, NULL));
}

void QueueApplication::InjectOsNetworkDisconnectedEvent() {
  Inject(new Event(kSbEventTypeOsNetworkDisconnected, NULL, NULL));
}

void QueueApplication::InjectOsNetworkConnectedEvent() {
  Inject(new Event(kSbEventTypeOsNetworkConnected, NULL, NULL));
}

void QueueApplication::WindowSizeChanged(void* context,
                                    EventHandledCallback callback) {
  Inject(new Event(kSbEventTypeWindowSizeChanged, context, callback));
}

SbEventId QueueApplication::Schedule(SbEventCallback callback,
                                void* context,
                                int64_t delay) {
  SbEventId id = ++g_next_event_id;
  InjectTimedEvent(new TimedEvent(id, callback, context, delay));
  return id;
}

void QueueApplication::Cancel(SbEventId id) {
  CancelTimedEvent(id);
}

void QueueApplication::HandleFrame(SbPlayer player,
                              const scoped_refptr<VideoFrame>& frame,
                              int z_index,
                              int x,
                              int y,
                              int width,
                              int height) {
  AcceptFrame(player, frame, z_index, x, y, width, height);
}

void QueueApplication::SetStartLink(const char* start_link) {
  SB_DCHECK(pthread_equal(thread_, pthread_self()));
  free(start_link_);
  if (start_link) {
    start_link_ = strdup(start_link);
  } else {
    start_link_ = NULL;
  }
}

void QueueApplication::DispatchStart(int64_t timestamp) {
  SB_DCHECK(pthread_equal(thread_, pthread_self()));
  SB_DCHECK_EQ(state_, kStateUnstarted);
  DispatchAndDelete(CreateInitialEvent(kSbEventTypeStart, timestamp));
}

void QueueApplication::DispatchPreload(int64_t timestamp) {
  SB_DCHECK(pthread_equal(thread_, pthread_self()));
  SB_DCHECK_EQ(state_, kStateUnstarted);
  DispatchAndDelete(CreateInitialEvent(kSbEventTypePreload, timestamp));
}

bool QueueApplication::HasPreloadSwitch() {
  return GetCommandLine()->HasSwitch(kPreloadSwitch);
}

bool QueueApplication::DispatchAndDelete(QueueApplication::Event* event) {
  SB_DCHECK(pthread_equal(thread_, pthread_self()));
  if (!event) {
    return true;
  }

  // Ensure the event is deleted unless it is released.
  std::unique_ptr<Event> scoped_event(event);

  // Ensure that we go through the the appropriate lifecycle events based on
  // the current state. If intermediate events need to be processed, use
  // HandleEventAndUpdateState() rather than Inject() for the intermediate
  // events because there may already be other lifecycle events in the queue.

  int64_t timestamp = scoped_event->event->timestamp;
  switch (scoped_event->event->type) {
    case kSbEventTypePreload:
      if (state_ != kStateUnstarted) {
        return true;
      }
      break;
    case kSbEventTypeStart:
      if (state_ != kStateUnstarted && state_ != kStateStarted) {
        HandleEventAndUpdateState(
            new Event(kSbEventTypeFocus, timestamp, NULL, NULL));
        return true;
      }
      break;
    case kSbEventTypeBlur:
      if (state_ != kStateStarted) {
        return true;
      }
      break;
    case kSbEventTypeFocus:
      switch (state_) {
        case kStateStopped:
          return true;
        case kStateFrozen:
          HandleEventAndUpdateState(
              new Event(kSbEventTypeUnfreeze, timestamp, NULL, NULL));
          [[fallthrough]];
        case kStateConcealed:
          HandleEventAndUpdateState(
              new Event(kSbEventTypeReveal, timestamp, NULL, NULL));
          HandleEventAndUpdateState(scoped_event.release());
          return true;
        case kStateBlurred:
          break;
        case kStateStarted:
        case kStateUnstarted:
          return true;
      }
      break;
    case kSbEventTypeConceal:
      switch (state_) {
        case kStateUnstarted:
          return true;
        case kStateStarted:
          HandleEventAndUpdateState(
              new Event(kSbEventTypeBlur, timestamp, NULL, NULL));
          HandleEventAndUpdateState(scoped_event.release());
          return true;
        case kStateBlurred:
          break;
        case kStateConcealed:
        case kStateFrozen:
        case kStateStopped:
          return true;
      }
      break;
    case kSbEventTypeReveal:
      switch (state_) {
        case kStateStopped:
          return true;
        case kStateFrozen:
          HandleEventAndUpdateState(
              new Event(kSbEventTypeUnfreeze, timestamp, NULL, NULL));
          HandleEventAndUpdateState(scoped_event.release());
          return true;
        case kStateConcealed:
          break;
        case kStateBlurred:
        case kStateStarted:
        case kStateUnstarted:
          return true;
      }
      break;
    case kSbEventTypeFreeze:
      switch (state_) {
        case kStateUnstarted:
          return true;
        case kStateStarted:
          HandleEventAndUpdateState(
              new Event(kSbEventTypeBlur, timestamp, NULL, NULL));
          [[fallthrough]];
        case kStateBlurred:
          HandleEventAndUpdateState(
              new Event(kSbEventTypeConceal, timestamp, NULL, NULL));
          HandleEventAndUpdateState(scoped_event.release());
          return true;
        case kStateConcealed:
          break;
        case kStateFrozen:
        case kStateStopped:
          return true;
      }
      break;
    case kSbEventTypeUnfreeze:
      switch (state_) {
        case kStateStopped:
          return true;
        case kStateFrozen:
          break;
        case kStateConcealed:
        case kStateBlurred:
        case kStateStarted:
        case kStateUnstarted:
          return true;
      }
      break;
    case kSbEventTypeStop:
      switch (state_) {
        case kStateUnstarted:
          return true;
        case kStateStarted:
          HandleEventAndUpdateState(
              new Event(kSbEventTypeBlur, timestamp, NULL, NULL));
          [[fallthrough]];
        case kStateBlurred:
          HandleEventAndUpdateState(
              new Event(kSbEventTypeConceal, timestamp, NULL, NULL));
          [[fallthrough]];
        case kStateConcealed:
          HandleEventAndUpdateState(
              new Event(kSbEventTypeFreeze, timestamp, NULL, NULL));
          // There is a race condition with kSbEventTypeStop processing and
          // timed events currently in use. Processing the intermediate events
          // takes time, so makes it more likely that a timed event will be due
          // immediately and processed immediately afterward. The event(s) need
          // to be fixed to behave better after kSbEventTypeStop has been
          // handled. In the meantime, continue to use Inject() to preserve the
          // current timing. This bug can still happen with Inject(), but it is
          // less likely than if HandleEventAndUpdateState() were used.
          Inject(scoped_event.release());
          return true;
        case kStateFrozen:
          break;
        case kStateStopped:
          return true;
      }
      error_level_ = scoped_event->error_level;
      break;
    case kSbEventTypeScheduled: {
      TimedEvent* timed_event =
          reinterpret_cast<TimedEvent*>(scoped_event->event->data);
      timed_event->callback(timed_event->context);
      return true;
    }
    default:
      break;
  }

  return HandleEventAndUpdateState(scoped_event.release());
}

bool QueueApplication::HandleEventAndUpdateState(QueueApplication::Event* event) {
  // Ensure the event is deleted unless it is released.
  std::unique_ptr<Event> scoped_event(event);

  // Call OnSuspend() and OnResume() before the event as needed.
  if (scoped_event->event->type == kSbEventTypeUnfreeze &&
      state_ == kStateFrozen) {
    OnResume();
  } else if (scoped_event->event->type == kSbEventTypeFreeze &&
             state_ == kStateConcealed) {
    OnSuspend();
  }

  sb_event_handle_callback_(scoped_event->event);

  switch (scoped_event->event->type) {
    case kSbEventTypePreload:
      SB_DCHECK_EQ(state_, kStateUnstarted);
      state_ = kStateConcealed;
      break;
    case kSbEventTypeStart:
      SB_DCHECK_EQ(state_, kStateUnstarted);
      state_ = kStateStarted;
      break;
    case kSbEventTypeBlur:
      SB_DCHECK_EQ(state_, kStateStarted);
      state_ = kStateBlurred;
      break;
    case kSbEventTypeFocus:
      SB_DCHECK_EQ(state_, kStateBlurred);
      state_ = kStateStarted;
      break;
    case kSbEventTypeConceal:
      SB_DCHECK_EQ(state_, kStateBlurred);
      state_ = kStateConcealed;
      break;
    case kSbEventTypeReveal:
      SB_DCHECK_EQ(state_, kStateConcealed);
      state_ = kStateBlurred;
      break;
    case kSbEventTypeFreeze:
      SB_DCHECK_EQ(state_, kStateConcealed);
      state_ = kStateFrozen;
      break;
    case kSbEventTypeUnfreeze:
      SB_DCHECK_EQ(state_, kStateFrozen);
      state_ = kStateConcealed;
      break;
    case kSbEventTypeStop:
      SB_DCHECK_EQ(state_, kStateFrozen);
      state_ = kStateStopped;
      return false;
    default:
      break;
  }
  // Should not be unstarted after the first event.
  SB_DCHECK_NE(state_, kStateUnstarted);
  return true;
}

void QueueApplication::CallTeardownCallbacks() {
  std::lock_guard lock(callbacks_lock_);
  for (size_t i = 0; i < teardown_callbacks_.size(); ++i) {
    teardown_callbacks_[i]();
  }
}

QueueApplication::Event* QueueApplication::CreateInitialEvent(SbEventType type,
                                                    int64_t timestamp) {
  SB_DCHECK(type == kSbEventTypePreload || type == kSbEventTypeStart);
  SbEventStartData* start_data = new SbEventStartData();
  memset(start_data, 0, sizeof(SbEventStartData));
  const CommandLine::StringVector& args = GetCommandLine()->argv();
  start_data->argument_count = static_cast<int>(args.size());
  // Cobalt web_platform_tests expect an extra argv[argc] set to NULL.
  start_data->argument_values = new char*[start_data->argument_count + 1];
  start_data->argument_values[start_data->argument_count] = NULL;
  for (int i = 0; i < start_data->argument_count; i++) {
    start_data->argument_values[i] = const_cast<char*>(args[i].c_str());
  }
  start_data->link = start_link_;

  return new Event(type, timestamp, start_data, &DeleteStartData);
}

int QueueApplication::RunLoop() {
  SB_DCHECK(GetCommandLine());
  if (IsPreloadImmediate()) {
    DispatchPreload(CurrentMonotonicTime());
  } else if (IsStartImmediate()) {
    DispatchStart(CurrentMonotonicTime());
  }

  for (;;) {
    if (!DispatchNextEvent()) {
      break;
    }
  }

  CallTeardownCallbacks();
  Teardown();
  return error_level_;
}

void QueueApplication::Wake() {
  if (pthread_equal(thread_, pthread_self())) {
    return;
  }

  if (!MayHaveSystemEvents()) {
    event_queue_->Wake();
    return;
  } else {
    WakeSystemEventWait();
  }
}

QueueApplication::Event* QueueApplication::GetNextEvent() {
  if (!MayHaveSystemEvents()) {
    return GetNextInjectedEvent();
  }

  // The construction of this loop is somewhat deliberate. The main UI message
  // pump will inject an event every time it needs to do deferred work. If we
  // don't prioritize system window events, they can get starved by a constant
  // stream of work.
  Event* event = NULL;
  while (!event) {
    event = PollNextSystemEvent();
    if (event) {
      continue;
    }

    // Then poll the generic queue.
    event = PollNextInjectedEvent();
    if (event) {
      continue;
    }

    // Then we block indefinitely on the Window's DFB queue.
    event = WaitForSystemEventWithTimeout(GetNextTimedEventTargetTime() -
                                          CurrentMonotonicTime());
  }

  return event;
}

void QueueApplication::Inject(QueueApplication::Event* event) {
  event_queue_->Put(event);
  if (MayHaveSystemEvents()) {
    WakeSystemEventWait();
  }
}

void QueueApplication::InjectTimedEvent(QueueApplication::TimedEvent* timed_event) {
  if (timed_event_queue_->Inject(timed_event)) {
    // The time to wake up has moved earlier, so wake up the event queue to
    // recalculate the wait.
    Wake();
  }
}

void QueueApplication::CancelTimedEvent(SbEventId event_id) {
  timed_event_queue_->Cancel(event_id);

  // The wait duration will only get longer after cancelling an event, so the
  // waiter will wake up as previously scheduled, see there is nothing to do,
  // and go back to sleep.
}

void QueueApplication::InjectAndProcess(SbEventType type,
                                        bool checkSystemEvents) {
  std::atomic_bool event_processed{false};
  Event* flagged_event = new Event(
      type, const_cast<std::atomic_bool*>(&event_processed), [](void* flag) {
        auto* bool_flag =
            const_cast<std::atomic_bool*>(static_cast<std::atomic_bool*>(flag));
        bool_flag->store(true);
      });
  Inject(flagged_event);
  while (!event_processed.load()) {
    if (checkSystemEvents) {
      DispatchAndDelete(GetNextEvent());
    } else {
      DispatchAndDelete(GetNextInjectedEvent());
    }
  }
}

QueueApplication::TimedEvent* QueueApplication::GetNextDueTimedEvent() {
  return timed_event_queue_->Get();
}

int64_t QueueApplication::GetNextTimedEventTargetTime() {
  return timed_event_queue_->GetTime();
}

QueueApplication::TimedEventQueue::TimedEventQueue() : set_(&IsLess) {}

QueueApplication::TimedEventQueue::~TimedEventQueue() {
  std::lock_guard lock(mutex_);
  for (TimedEventMap::iterator i = map_.begin(); i != map_.end(); ++i) {
    delete i->second;
  }
  map_.clear();
  set_.clear();
}

bool QueueApplication::TimedEventQueue::Inject(QueueApplication::TimedEvent* timed_event) {
  std::lock_guard lock(mutex_);
  int64_t oldTime = GetTimeLocked();
  map_[timed_event->id] = timed_event;
  set_.insert(timed_event);
  return (timed_event->target_time < oldTime);
}

void QueueApplication::TimedEventQueue::Cancel(SbEventId event_id) {
  std::lock_guard lock(mutex_);
  TimedEventMap::iterator i = map_.find(event_id);
  if (i == map_.end()) {
    return;
  }

  TimedEvent* timed_event = i->second;
  map_.erase(i);
  set_.erase(timed_event);
  delete timed_event;
}

QueueApplication::TimedEvent* QueueApplication::TimedEventQueue::Get() {
  std::lock_guard lock(mutex_);
  if (set_.empty()) {
    return NULL;
  }

  TimedEvent* timed_event = *(set_.begin());
  if (timed_event->target_time > CurrentMonotonicTime()) {
    return NULL;
  }

  map_.erase(timed_event->id);
  set_.erase(timed_event);
  return timed_event;
}

int64_t QueueApplication::TimedEventQueue::GetTime() {
  std::lock_guard lock(mutex_);
  return GetTimeLocked();
}

int64_t QueueApplication::TimedEventQueue::GetTimeLocked() {
  if (set_.empty()) {
    return std::numeric_limits<int64_t>::max();
  }

  TimedEvent* timed_event = *(set_.begin());
  int64_t time = timed_event->target_time;
  int64_t now = CurrentMonotonicTime();
  if (time < now) {
    return now;
  }

  return time;
}

// static
bool QueueApplication::TimedEventQueue::IsLess(const TimedEvent* lhs,
                                               const TimedEvent* rhs) {
  int64_t time_difference = lhs->target_time - rhs->target_time;
  if (time_difference != 0) {
    return time_difference < 0;
  }

  // If the time differences are the same, ensure there is a strict and stable
  // ordering.
  return reinterpret_cast<uintptr_t>(lhs) < reinterpret_cast<uintptr_t>(rhs);
}

QueueApplication::Event* QueueApplication::PollNextInjectedEvent() {
  Event* event = event_queue_->Poll();
  if (event != NULL) {
    return event;
  }

  TimedEvent* timed_event = GetNextDueTimedEvent();
  if (timed_event != NULL) {
    return new Event(timed_event);
  }

  return NULL;
}

QueueApplication::Event* QueueApplication::GetNextInjectedEvent() {
  for (;;) {
    int64_t delay = GetNextTimedEventTargetTime() - CurrentMonotonicTime();
    Event* event = event_queue_->GetTimed(delay);
    if (event != NULL) {
      return event;
    }

    TimedEvent* timed_event = GetNextDueTimedEvent();
    if (timed_event != NULL) {
      return new Event(timed_event);
    }
  }
}

}  // namespace starboard
}  // namespace shared
}  // namespace starboard