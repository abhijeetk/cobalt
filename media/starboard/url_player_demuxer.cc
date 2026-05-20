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

#include "media/starboard/url_player_demuxer.h"

#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "media/base/audio_decoder_config.h"
#include "media/base/channel_layout.h"
#include "media/base/decoder_buffer.h"
#include "media/base/media_track.h"
#include "media/base/sample_format.h"
#include "media/base/video_decoder_config.h"

namespace media {

// --- UrlPlayerDemuxerStream ---

UrlPlayerDemuxerStream::UrlPlayerDemuxerStream(Type type) : type_(type) {
  LOG(INFO) << "UrlPlayerDemuxerStream created: type=" << GetTypeName(type);
}

UrlPlayerDemuxerStream::~UrlPlayerDemuxerStream() = default;

void UrlPlayerDemuxerStream::Read(uint32_t count, ReadCB read_cb) {
  // URL player handles all buffering. Never called.
  LOG(WARNING) << "UrlPlayerDemuxerStream::Read() should not be called.";
  std::move(read_cb).Run(Status::kAborted, {});
}

AudioDecoderConfig UrlPlayerDemuxerStream::audio_decoder_config() {
  // Provide a minimal valid AAC config so the pipeline accepts the stream.
  return AudioDecoderConfig(AudioCodec::kAAC, kSampleFormatS16,
                            CHANNEL_LAYOUT_STEREO,
                            /*samples_per_second=*/44100,
                            /*extra_data=*/{}, EncryptionScheme::kUnencrypted);
}

VideoDecoderConfig UrlPlayerDemuxerStream::video_decoder_config() {
  // Provide a minimal valid H.264 config so the pipeline accepts the stream.
  return VideoDecoderConfig(
      VideoCodec::kH264, VideoCodecProfile::H264PROFILE_MAIN,
      VideoDecoderConfig::AlphaMode::kIsOpaque, VideoColorSpace(),
      kNoTransformation, gfx::Size(1920, 1080), gfx::Rect(1920, 1080),
      gfx::Size(1920, 1080), /*extra_data=*/{}, EncryptionScheme::kUnencrypted);
}

DemuxerStream::Type UrlPlayerDemuxerStream::type() const {
  return type_;
}

bool UrlPlayerDemuxerStream::SupportsConfigChanges() {
  return false;
}

// --- UrlPlayerDemuxer ---

UrlPlayerDemuxer::UrlPlayerDemuxer(GURL url) : url_(std::move(url)) {
  LOG(INFO) << "[URL-ROUTING] UrlPlayerDemuxer created with URL: "
            << url_.spec();
}

UrlPlayerDemuxer::~UrlPlayerDemuxer() {
  LOG(INFO) << "[URL-ROUTING] UrlPlayerDemuxer destroyed.";
}

std::vector<DemuxerStream*> UrlPlayerDemuxer::GetAllStreams() {
  return {&audio_stream_, &video_stream_};
}

GURL UrlPlayerDemuxer::GetMediaUrl() const {
  LOG(INFO) << "[URL-ROUTING] UrlPlayerDemuxer::GetMediaUrl() → "
            << url_.spec();
  return url_;
}

std::string UrlPlayerDemuxer::GetDisplayName() const {
  return "UrlPlayerDemuxer";
}

DemuxerType UrlPlayerDemuxer::GetDemuxerType() const {
  return DemuxerType::kUrlPlayerDemuxer;
}

void UrlPlayerDemuxer::Initialize(DemuxerHost* host,
                                  PipelineStatusCallback status_cb) {
  LOG(INFO) << "UrlPlayerDemuxer::Initialize() - immediately succeeding.";
  // Post the callback to avoid reentrancy issues.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(status_cb), PIPELINE_OK));
}

void UrlPlayerDemuxer::AbortPendingReads() {}
void UrlPlayerDemuxer::StartWaitingForSeek(base::TimeDelta seek_time) {}
void UrlPlayerDemuxer::CancelPendingSeek(base::TimeDelta seek_time) {}

void UrlPlayerDemuxer::Seek(base::TimeDelta time,
                            PipelineStatusCallback status_cb) {
  LOG(INFO) << "UrlPlayerDemuxer::Seek() to " << time;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(status_cb), PIPELINE_OK));
}

bool UrlPlayerDemuxer::IsSeekable() const {
  return true;
}

void UrlPlayerDemuxer::Stop() {
  LOG(INFO) << "UrlPlayerDemuxer::Stop()";
}

base::TimeDelta UrlPlayerDemuxer::GetStartTime() const {
  return base::TimeDelta();
}

base::Time UrlPlayerDemuxer::GetTimelineOffset() const {
  return base::Time();
}

int64_t UrlPlayerDemuxer::GetMemoryUsage() const {
  return 0;
}

std::optional<container_names::MediaContainerName>
UrlPlayerDemuxer::GetContainerForMetrics() const {
  return std::nullopt;
}

void UrlPlayerDemuxer::OnTracksChanged(
    DemuxerStream::Type track_type,
    const std::vector<MediaTrack::Id>& track_ids,
    base::TimeDelta curr_time,
    TrackChangeCB change_completed_cb) {}

void UrlPlayerDemuxer::SetPlaybackRate(double rate) {}

}  // namespace media
