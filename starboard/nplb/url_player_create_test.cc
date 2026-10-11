// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "build/build_config.h"
#include "starboard/common/log.h"
#include "starboard/common/paths.h"
#include "starboard/common/queue.h"
#include "starboard/common/time.h"
#include "starboard/player.h"
#include "starboard/shared/starboard/thread_checker.h"
#include "starboard/testing/test_runner.h"

#if BUILDFLAG(IS_IOS_TVOS)
#include "starboard/tvos/shared/media/url_player.h"
#endif  // BUILDFLAG(IS_IOS_TVOS)

#include "starboard/window.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace nplb {
namespace {

#if BUILDFLAG(IS_IOS_TVOS)

const char kPlayerUrl[] = "about:blank";

void DummyPlayerStatusFunc(SbPlayer player,
                           void* context,
                           SbPlayerState state,
                           int ticket) {}

void DummyEncryptedMediaInitaDataEncounteredFunc(
    SbPlayer player,
    void* context,
    const char* init_data_type,
    const unsigned char* init_data,
    unsigned int init_data_length) {}

void DummyPlayerErrorFunc(SbPlayer player,
                          void* context,
                          SbPlayerError error,
                          const char* message) {}

TEST(SbPlayerUrlTest, SunnyDay) {
  SbWindowOptions window_options;
  SbWindowSetDefaultOptions(&window_options);
  SbWindow window = SbWindowCreate(&window_options);

  SbPlayerOutputMode output_modes[] = {kSbPlayerOutputModeDecodeToTexture,
                                       kSbPlayerOutputModePunchOut};

  for (int i = 0; i < SB_ARRAY_SIZE_INT(output_modes); ++i) {
    SbPlayerOutputMode output_mode = output_modes[i];
    if (!SbUrlPlayerOutputModeSupported(output_mode)) {
      continue;
    }
    SbPlayer player =
        SbUrlPlayerCreate(kPlayerUrl, window, DummyPlayerStatusFunc,
                          DummyEncryptedMediaInitaDataEncounteredFunc,
                          DummyPlayerErrorFunc, NULL);

    EXPECT_TRUE(SbPlayerIsValid(player));

    SbPlayerDestroy(player);
  }

  SbWindowDestroy(window);
}

TEST(SbPlayerUrlTest, NullCallbacks) {
  SbWindowOptions window_options;
  SbWindowSetDefaultOptions(&window_options);
  SbWindow window = SbWindowCreate(&window_options);

  SbPlayerOutputMode output_modes[] = {kSbPlayerOutputModeDecodeToTexture,
                                       kSbPlayerOutputModePunchOut};

  for (int i = 0; i < SB_ARRAY_SIZE_INT(output_modes); ++i) {
    SbPlayerOutputMode output_mode = output_modes[i];
    if (!SbUrlPlayerOutputModeSupported(output_mode)) {
      continue;
    }
    {
      SbPlayer player =
          SbUrlPlayerCreate(kPlayerUrl, window, NULL /* player_status_func */,
                            DummyEncryptedMediaInitaDataEncounteredFunc,
                            DummyPlayerErrorFunc, NULL /* context */);
      EXPECT_FALSE(SbPlayerIsValid(player));
      SbPlayerDestroy(player);
    }
    {
      SbPlayer player = SbUrlPlayerCreate(
          kPlayerUrl, window, DummyPlayerStatusFunc,
          NULL /* encrypted_media_inita_data_encountered_func */,
          DummyPlayerErrorFunc, NULL /* context */);
      EXPECT_FALSE(SbPlayerIsValid(player));
      SbPlayerDestroy(player);
    }
    {
      SbPlayer player =
          SbUrlPlayerCreate(kPlayerUrl, window, DummyPlayerStatusFunc,
                            DummyEncryptedMediaInitaDataEncounteredFunc,
                            NULL /* player_error_func */, NULL /* context */);
      EXPECT_FALSE(SbPlayerIsValid(player));
      SbPlayerDestroy(player);
    }
  }
}

TEST(SbPlayerUrlTest, MultiPlayer) {
  SbWindowOptions window_options;
  SbWindowSetDefaultOptions(&window_options);
  SbWindow window = SbWindowCreate(&window_options);

  SbPlayerOutputMode output_modes[] = {kSbPlayerOutputModeDecodeToTexture,
                                       kSbPlayerOutputModePunchOut};

  for (int i = 0; i < SB_ARRAY_SIZE_INT(output_modes); ++i) {
    SbPlayerOutputMode output_mode = output_modes[i];
    if (!SbUrlPlayerOutputModeSupported(output_mode)) {
      continue;
    }
    const int kMaxPlayers = 16;
    std::vector<SbPlayer> created_players;
    for (int j = 0; j < kMaxPlayers; ++j) {
      created_players.push_back(
          SbUrlPlayerCreate(kPlayerUrl, window, NULL, NULL, NULL, NULL));
      if (!SbPlayerIsValid(created_players[j])) {
        created_players.pop_back();
        break;
      }
    }
    SB_DLOG(INFO) << "Created " << created_players.size()
                  << " valid players for output mode " << output_mode;
    for (auto player : created_players) {
      SbPlayerDestroy(player);
    }
  }
  SbWindowDestroy(window);
}

// Bundled by //starboard/nplb:url_player_test_data.
const char kFixturePath[] = "test/starboard/nplb/url_player/bear-1280x720.mp4";
const char kMissingFixturePath[] =
    "test/starboard/nplb/url_player/does-not-exist.mp4";
// Duration of the fixture, from its 'mvhd' box.
constexpr int64_t kFixtureDuration = 2'761'667;
constexpr int64_t kPrepareTimeout = 10'000'000;
constexpr int64_t kSeekTimeout = 5'000'000;
// Allows late or duplicate callbacks to arrive.
constexpr int64_t kSettleTime = 1'000'000;
constexpr int kSeekTicket = 1;

std::string GetFixtureUrl(const char* relative_path) {
  const std::string path = starboard::PrependContentPath(relative_path);
  EXPECT_FALSE(path.empty());
  return "file://" + path;
}

// Follows SbPlayerTestFixture: callbacks arrive on the player's threads and
// are queued, while the test thread drains the queue and pumps the main loop
// through starboard::RunTestBlockingAction().
class SbUrlPlayerPrepareTest : public ::testing::Test {
 protected:
  struct CallbackEvent {
    enum Type { kEmpty, kPlayerState, kError };

    Type type = kEmpty;
    SbPlayerState player_state = kSbPlayerStateInitialized;
    SbPlayerError error = kSbPlayerErrorDecode;
    int ticket = SB_PLAYER_INITIAL_TICKET;
  };

  void TearDown() override {
    if (SbPlayerIsValid(player_)) {
      DestroyPlayer();
    }
  }

  void CreatePlayer(const char* fixture_path) {
    player_ = SbUrlPlayerCreate(GetFixtureUrl(fixture_path).c_str(),
                                kSbWindowInvalid, &PlayerStatusCallback,
                                DummyEncryptedMediaInitaDataEncounteredFunc,
                                &ErrorCallback, this);
    ASSERT_TRUE(SbPlayerIsValid(player_));
  }

  void DestroyPlayer() {
    SbPlayerDestroy(player_);
    player_ = kSbPlayerInvalid;
  }

  // Records every event seen on the way.
  void WaitForPlayerState(SbPlayerState desired_state,
                          int ticket,
                          int64_t timeout) {
    const int64_t wait_end = starboard::CurrentMonotonicTime() + timeout;
    do {
      if (HasReceivedPlayerState(desired_state, ticket)) {
        return;
      }
    } while (
        ProcessNextEvent(wait_end - starboard::CurrentMonotonicTime()).type !=
        CallbackEvent::kEmpty);

    FAIL() << "WaitForPlayerState() did not receive state " << desired_state
           << " with ticket " << ticket << ".";
  }

  // Records every event seen on the way.
  void WaitForError(int64_t timeout) {
    const int64_t wait_end = starboard::CurrentMonotonicTime() + timeout;
    do {
      if (!errors_.empty()) {
        return;
      }
    } while (
        ProcessNextEvent(wait_end - starboard::CurrentMonotonicTime()).type !=
        CallbackEvent::kEmpty);

    FAIL() << "WaitForError() did not receive an error.";
  }

  void PumpFor(int64_t duration) {
    const int64_t wait_end = starboard::CurrentMonotonicTime() + duration;
    while (
        ProcessNextEvent(wait_end - starboard::CurrentMonotonicTime()).type !=
        CallbackEvent::kEmpty) {
    }
  }

  void ExpectNoMoreCallbacks(int64_t duration) {
    const int64_t wait_end = starboard::CurrentMonotonicTime() + duration;
    for (;;) {
      const CallbackEvent event =
          ProcessNextEvent(wait_end - starboard::CurrentMonotonicTime());
      if (event.type == CallbackEvent::kEmpty) {
        return;
      }
      if (event.type == CallbackEvent::kPlayerState) {
        ADD_FAILURE() << "Unexpected player state " << event.player_state
                      << " with ticket " << event.ticket << ".";
      } else {
        ADD_FAILURE() << "Unexpected error " << event.error << ".";
      }
    }
  }

  bool HasReceivedPlayerState(SbPlayerState state, int ticket) const {
    return player_state_set_.find(std::make_pair(state, ticket)) !=
           player_state_set_.end();
  }

  // True if |state| was received with any ticket.
  bool HasReceivedPlayerState(SbPlayerState state) const {
    return std::any_of(player_state_set_.begin(), player_state_set_.end(),
                       [state](const std::pair<SbPlayerState, int>& received) {
                         return received.first == state;
                       });
  }

  const std::vector<SbPlayerError>& errors() const { return errors_; }

  SbPlayer player_ = kSbPlayerInvalid;

 private:
  static void PlayerStatusCallback(SbPlayer player,
                                   void* context,
                                   SbPlayerState state,
                                   int ticket) {
    SB_LOG(INFO) << "URL player state " << state << " ticket " << ticket;
    CallbackEvent event;
    event.type = CallbackEvent::kPlayerState;
    event.player_state = state;
    event.ticket = ticket;
    static_cast<SbUrlPlayerPrepareTest*>(context)->callback_event_queue_.Put(
        event);
  }

  static void ErrorCallback(SbPlayer player,
                            void* context,
                            SbPlayerError error,
                            const char* message) {
    SB_LOG(INFO) << "URL player error " << error << ": " << message;
    CallbackEvent event;
    event.type = CallbackEvent::kError;
    event.error = error;
    static_cast<SbUrlPlayerPrepareTest*>(context)->callback_event_queue_.Put(
        event);
  }

  // Returns a kEmpty event if none arrived within |timeout|.
  CallbackEvent ProcessNextEvent(int64_t timeout) {
    SB_CHECK(thread_checker_.CalledOnValidThread());
    if (timeout <= 0) {
      return CallbackEvent();
    }

    // RunTestBlockingAction() is necessary to prevent deadlocks during NPLB
    // tests on platforms such as tvOS, where the main thread could be blocked
    // waiting for a condition variable while worker threads are simultaneously
    // blocked waiting for the main thread to handle application events.
    CallbackEvent event;
    starboard::RunTestBlockingAction(
        [&] { event = callback_event_queue_.GetTimed(timeout); });

    if (event.type == CallbackEvent::kPlayerState) {
      player_state_set_.insert(
          std::make_pair(event.player_state, event.ticket));
    } else if (event.type == CallbackEvent::kError) {
      errors_.push_back(event.error);
    }
    return event;
  }

  starboard::ThreadChecker thread_checker_;
  starboard::Queue<CallbackEvent> callback_event_queue_;
  std::set<std::pair<SbPlayerState, int>> player_state_set_;
  std::vector<SbPlayerError> errors_;
};

TEST_F(SbUrlPlayerPrepareTest, MetadataIsValidAtInitialized) {
  ASSERT_NO_FATAL_FAILURE(CreatePlayer(kFixturePath));

  ASSERT_NO_FATAL_FAILURE(WaitForPlayerState(
      kSbPlayerStateInitialized, SB_PLAYER_INITIAL_TICKET, kPrepareTimeout));

  SbPlayerInfo info = {};
  SbPlayerGetInfo(player_, &info);
  EXPECT_NEAR(info.duration, kFixtureDuration, 100'000);
  EXPECT_EQ(info.frame_width, 1280);
  EXPECT_EQ(info.frame_height, 720);

  // Loading the asset does not report Prerolling.
  EXPECT_FALSE(HasReceivedPlayerState(kSbPlayerStatePrerolling));
  EXPECT_TRUE(errors().empty());
  ExpectNoMoreCallbacks(kSettleTime);
}

TEST_F(SbUrlPlayerPrepareTest, PrepareFailureIsReportedWithoutInitialized) {
  ASSERT_NO_FATAL_FAILURE(CreatePlayer(kMissingFixturePath));

  ASSERT_NO_FATAL_FAILURE(WaitForError(kPrepareTimeout));

  ASSERT_FALSE(errors().empty());
  EXPECT_EQ(errors()[0],
            static_cast<SbPlayerError>(kSbUrlPlayerErrorSrcNotSupported));
  EXPECT_FALSE(HasReceivedPlayerState(kSbPlayerStateInitialized));
}

TEST_F(SbUrlPlayerPrepareTest, SeekAfterInitializedReachesPresenting) {
  ASSERT_NO_FATAL_FAILURE(CreatePlayer(kFixturePath));
  ASSERT_NO_FATAL_FAILURE(WaitForPlayerState(
      kSbPlayerStateInitialized, SB_PLAYER_INITIAL_TICKET, kPrepareTimeout));

  SbPlayerSeek(player_, 0, kSeekTicket);

  ASSERT_NO_FATAL_FAILURE(
      WaitForPlayerState(kSbPlayerStatePresenting, kSeekTicket, kSeekTimeout));
  EXPECT_TRUE(HasReceivedPlayerState(kSbPlayerStatePrerolling, kSeekTicket));
  EXPECT_TRUE(errors().empty());
}

TEST_F(SbUrlPlayerPrepareTest, SeekBeforeInitializedIsIgnored) {
  ASSERT_NO_FATAL_FAILURE(CreatePlayer(kFixturePath));

  SbPlayerSeek(player_, 0, kSeekTicket);

  // The seek is dropped, but it must not cost the player its preparation.
  ASSERT_NO_FATAL_FAILURE(WaitForPlayerState(
      kSbPlayerStateInitialized, SB_PLAYER_INITIAL_TICKET, kPrepareTimeout));

  // Drain before asserting: WaitForPlayerState() stops at the state it wants,
  // so a later Prerolling would otherwise go unrecorded and unnoticed.
  PumpFor(kSettleTime);
  EXPECT_FALSE(HasReceivedPlayerState(kSbPlayerStatePrerolling, kSeekTicket));
  EXPECT_TRUE(errors().empty());
}

#endif  // BUILDFLAG(IS_IOS_TVOS)

}  // namespace
}  // namespace nplb
