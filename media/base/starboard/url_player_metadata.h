// Copyright 2026 The Cobalt Authors. All Rights Reserved.
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

#ifndef MEDIA_BASE_STARBOARD_URL_PLAYER_METADATA_H_
#define MEDIA_BASE_STARBOARD_URL_PLAYER_METADATA_H_

#include "base/time/time.h"
#include "ui/gfx/geometry/size.h"

namespace media {

// Metadata of a prepared URL player resource. This C++ struct is the
// equivalent to mojom::UrlPlayerMetadata.
struct UrlPlayerMetadata {
  // kInfiniteDuration for live or unknown durations; never zero.
  base::TimeDelta duration;
  // Empty if not known yet.
  gfx::Size natural_size;
};

}  // namespace media

#endif  // MEDIA_BASE_STARBOARD_URL_PLAYER_METADATA_H_
