// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/renderers/video_overlay_factory.h"

#include "base/logging.h"
#include "base/time/time.h"
#include "media/base/video_frame.h"
#include "ui/gfx/geometry/size.h"
#include "base/command_line.h"
#include "base/process/process.h"
#include "base/threading/platform_thread.h"
#include "content/public/common/content_switches.h"

namespace media {

VideoOverlayFactory::VideoOverlayFactory()
    : overlay_plane_id_(base::UnguessableToken::Create()) {
  // [ABHIJEET][PUNCH-OUT] Log VideoOverlayFactory creation
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] VideoOverlayFactory CREATED"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName()
            << " | Overlay Plane ID: " << overlay_plane_id_.ToString()
            << " | REVERTED: Back to transparent holes (RED debug was blocking video)"
            << " | PURPOSE: Factory for creating transparent punch-out video holes";
}

VideoOverlayFactory::~VideoOverlayFactory() {
  // [ABHIJEET][PUNCH-OUT] Log VideoOverlayFactory destruction
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd && cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] VideoOverlayFactory DESTROYED"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName()
            << " | Overlay Plane ID: " << overlay_plane_id_.ToString()
            << " | PURPOSE: Cleaning up transparent hole frame factory";
}

scoped_refptr<VideoFrame> VideoOverlayFactory::CreateFrame(
    const gfx::Size& size) {
  // [ABHIJEET][PUNCH-OUT] Log CreateFrame call with detailed info
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  // Frame size empty => video has one dimension = 0.
  // Dimension 0 case triggers a DCHECK later on if we push through the overlay
  // path.
  if (size.IsEmpty()) {
    LOG(INFO) << "[ABHIJEET][PUNCH-OUT] VideoOverlayFactory::CreateFrame - EMPTY SIZE FALLBACK"
              << " | Process: " << process_name << " | PID: " << pid
              << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
              << " | Thread Name: " << base::PlatformThread::GetName()
              << " | Size: " << size.ToString() << " (EMPTY - creating black frame)"
              << " | PURPOSE: Fallback to black frame for empty video dimensions";
    return VideoFrame::CreateBlackFrame(gfx::Size(1, 1));
  }

  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] VideoOverlayFactory::CreateFrame - STEP 1/4: HOLE FRAME CREATION"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName()
            << " | Size: " << size.ToString()
            << " | Overlay Plane ID: " << overlay_plane_id_.ToString()
            << " | STEP: 1/4 - VideoOverlayFactory creates transparent holes for punch-out video"
            << " | PURPOSE: Creating transparent hole that compositor will replace with hardware overlay";
  
  // REVERTED: Create proper video hole frame instead of RED debug frame
  // CreateVideoHoleFrame creates special holes that compositor replaces with hardware overlay
  // CreateColorFrame was blocking video - it renders ON TOP instead of creating holes
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] VideoOverlayFactory::CreateFrame - CREATING TRANSPARENT VIDEO HOLE"
            << " | Method: VideoFrame::CreateVideoHoleFrame() - creates compositor holes"
            << " | Purpose: Transparent hole that OverlayStrategyUnderlayStarboard will replace"
            << " | Hardware Overlay: SbPlayer renders underneath this hole"
            << " | REVERTED: RED debug frames were blocking video playback";
            
  scoped_refptr<VideoFrame> frame =
      VideoFrame::CreateVideoHoleFrame(overlay_plane_id_,
                                       size,                // natural size
                                       base::TimeDelta());  // timestamp
  DCHECK(frame);
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] VideoOverlayFactory::CreateFrame - TRANSPARENT HOLE VALIDATION"
            << " | Frame Created: " << (frame ? "SUCCESS" : "FAILED")
            << " | Frame Valid: " << (frame && !frame->coded_size().IsEmpty() ? "YES" : "NO")
            << " | Compositor Processing: OverlayStrategyUnderlayStarboard will detect and replace this hole";
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] VideoOverlayFactory::CreateFrame - TRANSPARENT HOLE FRAME CREATED"
            << " | VideoFrame Format: " << frame->format()
            << " | Coded Size: " << frame->coded_size().ToString()
            << " | Natural Size: " << frame->natural_size().ToString()
            << " | Overlay Plane ID: " << overlay_plane_id_.ToString()
            << " | PURPOSE: Transparent hole for compositor to replace with hardware video overlay";
  
  return frame;
}

}  // namespace media
