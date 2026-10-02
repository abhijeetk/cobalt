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
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "build/build_config.h"
#include "starboard/common/log.h"
#include "starboard/common/time.h"
#include "starboard/configuration_constants.h"
#include "starboard/decode_target.h"
#include "starboard/player.h"
#include "starboard/system.h"
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

  // tvOS has no SbWindow; the URL player does not use |window|.
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

    if (output_mode == kSbPlayerOutputModeDecodeToTexture) {
      SbPlayerGetCurrentFrame(player);
    }

    SbPlayerDestroy(player);
  }

  SbWindowDestroy(window);
}

TEST(SbPlayerUrlTest, NullCallbacks) {
  SbWindowOptions window_options;
  SbWindowSetDefaultOptions(&window_options);

  // tvOS has no SbWindow; the URL player does not use |window|.
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

  // tvOS has no SbWindow; the URL player does not use |window|.
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
const char kFixturePath[] = "/test/starboard/nplb/url_player/bear-1280x720.mp4";
const char kMissingFixturePath[] =
    "/test/starboard/nplb/url_player/does-not-exist.mp4";
// Duration of the fixture, from its 'mvhd' box.
constexpr int64_t kFixtureDuration = 2'761'667;
constexpr int64_t kPrepareTimeout = 10'000'000;
constexpr int64_t kSeekTimeout = 5'000'000;
// Time allowed for late or duplicate callbacks to arrive.
constexpr int64_t kSettleTime = 1'000'000;

std::string GetFixtureUrl(const char* relative_path) {
  std::vector<char> content_path(kSbFileMaxPath);
  EXPECT_TRUE(SbSystemGetPath(kSbSystemPathContentDirectory,
                              content_path.data(), kSbFileMaxPath));
  return std::string("file://") + content_path.data() + relative_path;
}

// Records URL player callbacks, which may arrive on any thread.
class UrlPlayerCallbackRecorder {
 public:
  struct Status {
    SbPlayerState state;
    int ticket;
  };

  static void OnStatus(SbPlayer player,
                       void* context,
                       SbPlayerState state,
                       int ticket) {
    SB_LOG(INFO) << "URL player state " << state << " ticket " << ticket;
    auto* recorder = static_cast<UrlPlayerCallbackRecorder*>(context);
    std::lock_guard lock(recorder->mutex_);
    recorder->statuses_.push_back({state, ticket});
    recorder->condition_variable_.notify_all();
  }

  static void OnError(SbPlayer player,
                      void* context,
                      SbPlayerError error,
                      const char* message) {
    SB_LOG(INFO) << "URL player error " << error << ": " << message;
    auto* recorder = static_cast<UrlPlayerCallbackRecorder*>(context);
    std::lock_guard lock(recorder->mutex_);
    recorder->errors_.push_back(error);
    recorder->condition_variable_.notify_all();
  }

  SbPlayer CreatePlayer(const std::string& url, SbWindow window) {
    return SbUrlPlayerCreate(url.c_str(), window, &OnStatus,
                             DummyEncryptedMediaInitaDataEncounteredFunc,
                             &OnError, this);
  }

  int CountStatus(SbPlayerState state, int ticket) {
    std::lock_guard lock(mutex_);
    int count = 0;
    for (const Status& status : statuses_) {
      if (status.state == state && status.ticket == ticket) {
        ++count;
      }
    }
    return count;
  }

  int CountStatus(SbPlayerState state) {
    std::lock_guard lock(mutex_);
    int count = 0;
    for (const Status& status : statuses_) {
      if (status.state == state) {
        ++count;
      }
    }
    return count;
  }

  int CountCallbacks() {
    std::lock_guard lock(mutex_);
    return static_cast<int>(statuses_.size() + errors_.size());
  }

  std::vector<SbPlayerError> errors() {
    std::lock_guard lock(mutex_);
    return errors_;
  }

  // Pumps the main loop until |done| returns true or |timeout| expires.
  // Returns the final value of |done|.
  bool WaitFor(const std::function<bool()>& done, int64_t timeout) {
    const int64_t wait_end = starboard::CurrentMonotonicTime() + timeout;
    for (;;) {
      if (done()) {
        return true;
      }
      const int64_t now = starboard::CurrentMonotonicTime();
      if (now >= wait_end) {
        return false;
      }
      starboard::RunTestBlockingAction([&] {
        std::unique_lock lock(mutex_);
        condition_variable_.wait_for(
            lock, std::chrono::microseconds(
                      std::min<int64_t>(wait_end - now, 10'000)));
      });
    }
  }

  bool WaitForStatus(SbPlayerState state, int ticket, int64_t timeout) {
    return WaitFor([&] { return CountStatus(state, ticket) > 0; }, timeout);
  }

  void Settle(int64_t duration) {
    WaitFor([] { return false; }, duration);
  }

 private:
  std::mutex mutex_;
  std::condition_variable condition_variable_;
  std::vector<Status> statuses_;
  std::vector<SbPlayerError> errors_;
};

class SbUrlPlayerPrepareTest : public ::testing::Test {
 protected:
  void TearDown() override {
    if (SbPlayerIsValid(player_)) {
      DestroyPlayer();
    }
  }

  void CreatePlayer(const char* fixture_path) {
    player_ =
        recorder_.CreatePlayer(GetFixtureUrl(fixture_path), kSbWindowInvalid);
    ASSERT_TRUE(SbPlayerIsValid(player_));
  }

  // Returns the number of callbacks received when SbPlayerDestroy() returns.
  int DestroyPlayer() {
    SbPlayerDestroy(player_);
    player_ = kSbPlayerInvalid;
    const int callbacks = recorder_.CountCallbacks();
    // The platform destroys the player asynchronously on the main thread.
    recorder_.Settle(kSettleTime);
    return callbacks;
  }

  SbPlayerInfo GetInfo() {
    SbPlayerInfo info = {};
    SbPlayerGetInfo(player_, &info);
    return info;
  }

  UrlPlayerCallbackRecorder recorder_;
  SbPlayer player_ = kSbPlayerInvalid;
};

TEST_F(SbUrlPlayerPrepareTest, InitializedIsReportedOnceAfterCreate) {
  CreatePlayer(kFixturePath);
  EXPECT_EQ(recorder_.CountStatus(kSbPlayerStateInitialized), 0);

  ASSERT_TRUE(recorder_.WaitForStatus(
      kSbPlayerStateInitialized, SB_PLAYER_INITIAL_TICKET, kPrepareTimeout));
  recorder_.Settle(kSettleTime);
  EXPECT_EQ(recorder_.CountStatus(kSbPlayerStateInitialized), 1);
  EXPECT_TRUE(recorder_.errors().empty());
}

TEST_F(SbUrlPlayerPrepareTest, DurationAndSizeAreValidAtInitialized) {
  CreatePlayer(kFixturePath);
  ASSERT_TRUE(recorder_.WaitForStatus(
      kSbPlayerStateInitialized, SB_PLAYER_INITIAL_TICKET, kPrepareTimeout));

  SbPlayerInfo info = GetInfo();
  EXPECT_NEAR(info.duration, kFixtureDuration, 100'000);
  EXPECT_EQ(info.frame_width, 1280);
  EXPECT_EQ(info.frame_height, 720);
}

TEST_F(SbUrlPlayerPrepareTest, PrepareFailureIsReportedWithoutInitialized) {
  CreatePlayer(kMissingFixturePath);
  ASSERT_TRUE(recorder_.WaitFor([&] { return !recorder_.errors().empty(); },
                                kPrepareTimeout));
  EXPECT_EQ(recorder_.errors()[0],
            static_cast<SbPlayerError>(kSbUrlPlayerErrorSrcNotSupported));
  EXPECT_EQ(recorder_.CountStatus(kSbPlayerStateInitialized), 0);
}

TEST_F(SbUrlPlayerPrepareTest, SeekAfterInitializedReachesPresenting) {
  CreatePlayer(kFixturePath);
  ASSERT_TRUE(recorder_.WaitForStatus(
      kSbPlayerStateInitialized, SB_PLAYER_INITIAL_TICKET, kPrepareTimeout));
  SbPlayerSeek(player_, 0, 1);
  ASSERT_TRUE(
      recorder_.WaitForStatus(kSbPlayerStatePresenting, 1, kSeekTimeout));
  EXPECT_TRUE(recorder_.errors().empty());
}

TEST_F(SbUrlPlayerPrepareTest, SeekBeforeInitializedReportsError) {
  if (SB_DCHECK_ENABLED) {
    GTEST_SKIP() << "SbPlayerSeek() before Initialized fails a DCHECK.";
  }
  CreatePlayer(kFixturePath);
  SbPlayerSeek(player_, 0, 1);
  ASSERT_TRUE(recorder_.WaitFor([&] { return !recorder_.errors().empty(); },
                                kPrepareTimeout));
  EXPECT_EQ(recorder_.errors()[0], kSbPlayerErrorDecode);
  EXPECT_EQ(recorder_.CountStatus(kSbPlayerStatePrerolling, 1), 0);
  EXPECT_EQ(recorder_.CountStatus(kSbPlayerStatePresenting, 1), 0);
}

TEST_F(SbUrlPlayerPrepareTest, DestroyRightAfterCreate) {
  CreatePlayer(kFixturePath);
  const int callbacks = DestroyPlayer();
  EXPECT_EQ(recorder_.CountCallbacks(), callbacks);
}

TEST_F(SbUrlPlayerPrepareTest, DestroyDuringPrepare) {
  // Destroy at several points of the asynchronous prepare.
  for (int64_t delay : {10'000, 50'000, 100'000, 200'000}) {
    CreatePlayer(kFixturePath);
    recorder_.Settle(delay);
    const int callbacks = DestroyPlayer();
    EXPECT_EQ(recorder_.CountCallbacks(), callbacks) << "delay " << delay;
  }
}

TEST_F(SbUrlPlayerPrepareTest, DestroyAfterInitialized) {
  CreatePlayer(kFixturePath);
  ASSERT_TRUE(recorder_.WaitForStatus(
      kSbPlayerStateInitialized, SB_PLAYER_INITIAL_TICKET, kPrepareTimeout));
  DestroyPlayer();
}

#endif  // BUILDFLAG(IS_IOS_TVOS)

}  // namespace
}  // namespace nplb
