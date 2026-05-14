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

#ifndef MEDIA_STARBOARD_URL_PLAYER_DEMUXER_H_
#define MEDIA_STARBOARD_URL_PLAYER_DEMUXER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "media/base/demuxer.h"
#include "media/base/demuxer_stream.h"
#include "media/base/media_export.h"
#include "url/gurl.h"

namespace media {

// A stub DemuxerStream that provides a minimal valid config.
// Used by UrlPlayerDemuxer to satisfy Chromium pipeline requirements
// when the actual media handling is done by the platform's URL player
// (e.g., AVPlayer for HLS on tvOS).
class MEDIA_EXPORT UrlPlayerDemuxerStream : public DemuxerStream {
 public:
  explicit UrlPlayerDemuxerStream(Type type);
  ~UrlPlayerDemuxerStream() override;

  // DemuxerStream implementation.
  void Read(uint32_t count, ReadCB read_cb) override;
  AudioDecoderConfig audio_decoder_config() override;
  VideoDecoderConfig video_decoder_config() override;
  Type type() const override;
  bool SupportsConfigChanges() override;

 private:
  const Type type_;
};

// A stub Demuxer that provides dummy audio/video streams to satisfy
// Chromium's pipeline requirement for a non-null demuxer. The actual
// media playback is handled entirely by the platform's URL player
// (e.g., AVPlayer for HLS on tvOS). The pipeline will create this
// demuxer, initialize it, and pass it to the renderer. The renderer
// (StarboardRenderer) will detect source_url_ and ignore these
// dummy streams, using SbUrlPlayerCreate() instead.
class MEDIA_EXPORT UrlPlayerDemuxer : public Demuxer {
 public:
  explicit UrlPlayerDemuxer(GURL url);
  ~UrlPlayerDemuxer() override;

  // MediaResource implementation.
  std::vector<DemuxerStream*> GetAllStreams() override;
  GURL GetMediaUrl() const override;

  // Demuxer implementation.
  std::string GetDisplayName() const override;
  DemuxerType GetDemuxerType() const override;
  void Initialize(DemuxerHost* host, PipelineStatusCallback status_cb) override;
  void AbortPendingReads() override;
  void StartWaitingForSeek(base::TimeDelta seek_time) override;
  void CancelPendingSeek(base::TimeDelta seek_time) override;
  void Seek(base::TimeDelta time, PipelineStatusCallback status_cb) override;
  bool IsSeekable() const override;
  void Stop() override;
  base::TimeDelta GetStartTime() const override;
  base::Time GetTimelineOffset() const override;
  int64_t GetMemoryUsage() const override;
  std::optional<container_names::MediaContainerName> GetContainerForMetrics()
      const override;
  void OnTracksChanged(DemuxerStream::Type track_type,
                       const std::vector<MediaTrack::Id>& track_ids,
                       base::TimeDelta curr_time,
                       TrackChangeCB change_completed_cb) override;
  void SetPlaybackRate(double rate) override;

 private:
  const GURL url_;
  UrlPlayerDemuxerStream audio_stream_{DemuxerStream::AUDIO};
  UrlPlayerDemuxerStream video_stream_{DemuxerStream::VIDEO};
};

}  // namespace media

#endif  // MEDIA_STARBOARD_URL_PLAYER_DEMUXER_H_
