// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/common/quads/video_hole_draw_quad.h"

#include <stddef.h>
#include "base/trace_event/traced_value.h"
#include "base/values.h"
#include "base/logging.h"
#include "base/command_line.h"
#include "base/process/process.h"
#include "base/threading/platform_thread.h"
#include "content/public/common/content_switches.h"

namespace viz {

VideoHoleDrawQuad::VideoHoleDrawQuad() = default;

VideoHoleDrawQuad::VideoHoleDrawQuad(const VideoHoleDrawQuad& other) = default;

VideoHoleDrawQuad::~VideoHoleDrawQuad() = default;

void VideoHoleDrawQuad::SetNew(const SharedQuadState* shared_quad_state,
                               const gfx::Rect& rect,
                               const gfx::Rect& visible_rect,
                               const base::UnguessableToken& plane_id) {
  DrawQuad::SetAll(shared_quad_state, DrawQuad::Material::kVideoHole, rect,
                   visible_rect,
                   /*needs_blending=*/false);
  overlay_plane_id = plane_id;
  
  // [ABHIJEET][PUNCH-OUT] Log VideoHoleDrawQuad creation - COMPOSITOR THREAD
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] VideoHoleDrawQuad::SetNew - STEP 2/4: QUAD TREE INTEGRATION"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName()
            << " | Overlay Plane ID: " << plane_id.ToString()
            << " | Rect: " << rect.ToString()
            << " | Visible Rect: " << visible_rect.ToString()
            << " | STEP: 2/4 - VideoHoleDrawQuad integrates hole into compositor quad tree (GPU Process)"
            << " | PURPOSE: Hole quad created in compositor for transparent video area";
}

void VideoHoleDrawQuad::SetAll(const SharedQuadState* shared_quad_state,
                               const gfx::Rect& rect,
                               const gfx::Rect& visible_rect,
                               bool needs_blending,
                               const base::UnguessableToken& plane_id) {
  DrawQuad::SetAll(shared_quad_state, DrawQuad::Material::kVideoHole, rect,
                   visible_rect, needs_blending);
  overlay_plane_id = plane_id;
}

const VideoHoleDrawQuad* VideoHoleDrawQuad::MaterialCast(const DrawQuad* quad) {
  CHECK_EQ(quad->material, DrawQuad::Material::kVideoHole);
  return static_cast<const VideoHoleDrawQuad*>(quad);
}

void VideoHoleDrawQuad::ExtendValue(
    base::trace_event::TracedValue* value) const {
  value->SetString("overlay_plane_id", overlay_plane_id.ToString());
}

}  // namespace viz
