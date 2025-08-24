// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cobalt/media/service/video_geometry_setter_service.h"

#include <utility>

#include "base/command_line.h"
#include "base/process/process.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/platform_thread.h"
#include "content/public/common/content_switches.h"

#define MAKE_SURE_ON_SEQUENCE(callback, ...)                                   \
  if (!task_runner_->RunsTasksInCurrentSequence()) {                           \
    task_runner_->PostTask(                                                    \
        FROM_HERE, base::BindOnce(&VideoGeometrySetterService::callback,       \
                                  weak_factory_.GetWeakPtr(), ##__VA_ARGS__)); \
    return;                                                                    \
  }

namespace cobalt {
namespace media {

VideoGeometrySetterService::VideoGeometrySetterService()
    : task_runner_(base::SequencedTaskRunner::GetCurrentDefault()),
      weak_factory_(this) {
  
  // [ABHIJEET][PUNCH-OUT] Log VideoGeometrySetterService creation with process/thread info
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] VideoGeometrySetterService CREATED"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName()
            << " | PURPOSE: Central service for punch-out video geometry coordination";
            
  // IPC ARCHITECTURE DOCUMENTATION:
  // This service acts as the CENTRAL HUB for video geometry communication:
  // - HOST: Browser Process (where this service lives)
  // - CLIENTS: StarboardRendererClient (Renderer Process) + StarboardRenderer (GPU Process)
  // - GEOMETRY FLOW: StarboardRenderer → VideoGeometrySetter → VideoGeometryChangeClient
  // - SUBSCRIPTION FLOW: StarboardRendererClient ← VideoGeometryChangeSubscriber ← This Service
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] IPC GEOMETRY SERVICE ARCHITECTURE:"
            << " | THIS SERVICE HOST: Browser Process (CENTRAL HUB)"
            << " | GEOMETRY SETTERS: StarboardRenderer in GPU Process"
            << " | GEOMETRY SUBSCRIBERS: StarboardRendererClient in Renderer Process"
            << " | PURPOSE: Coordinates punch-out video geometry between processes";
}

VideoGeometrySetterService::~VideoGeometrySetterService() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
}

void VideoGeometrySetterService::GetVideoGeometryChangeSubscriber(
    mojo::PendingReceiver<mojom::VideoGeometryChangeSubscriber>
        pending_receiver) {
  MAKE_SURE_ON_SEQUENCE(GetVideoGeometryChangeSubscriber,
                        std::move(pending_receiver));
  
  // [ABHIJEET][PUNCH-OUT] Log VideoGeometryChangeSubscriber binding
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] VideoGeometryChangeSubscriber BINDING"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | HOST: Browser Process (THIS VideoGeometrySetterService)"
            << " | RECEIVER: Renderer Process (StarboardRendererClient)"
            << " | PURPOSE: Renderer subscribes to receive geometry change notifications"
            << " | FLOW: Browser → Renderer (geometry notifications)";
            
  video_geometry_change_subscriber_receivers_.Add(this,
                                                  std::move(pending_receiver));
}

base::RepeatingCallback<
    void(mojo::PendingReceiver<mojom::VideoGeometryChangeSubscriber>)>
VideoGeometrySetterService::GetBindSubscriberCallback() {
  return base::BindRepeating(
      &VideoGeometrySetterService::GetVideoGeometryChangeSubscriber,
      weak_factory_.GetWeakPtr());
}

void VideoGeometrySetterService::GetVideoGeometrySetter(
    mojo::PendingReceiver<mojom::VideoGeometrySetter> pending_receiver) {
  MAKE_SURE_ON_SEQUENCE(GetVideoGeometrySetter, std::move(pending_receiver));
  
  // [ABHIJEET][PUNCH-OUT] Log VideoGeometrySetter binding with detailed IPC info
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  if (video_geometry_setter_receiver_.is_bound()) {
    LOG(ERROR) << "[ABHIJEET][PUNCH-OUT] VideoGeometrySetter ALREADY BOUND - dropping previous"
               << " | Process: " << process_name << " | PID: " << pid
               << " | This indicates multiple StarboardRenderer instances trying to bind";
    video_geometry_setter_receiver_.reset();
  }
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] VideoGeometrySetter BINDING"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | HOST: Browser Process (THIS VideoGeometrySetterService)"
            << " | RECEIVER: GPU Process (StarboardRenderer)"
            << " | PURPOSE: GPU process can send geometry updates for punch-out video"
            << " | FLOW: GPU → Browser (video geometry updates)";
            
  video_geometry_setter_receiver_.Bind(std::move(pending_receiver));
}

void VideoGeometrySetterService::SubscribeToVideoGeometryChange(
    const base::UnguessableToken& overlay_plane_id,
    mojo::PendingRemote<mojom::VideoGeometryChangeClient> client_pending_remote,
    SubscribeToVideoGeometryChangeCallback callback) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  
  // CRITICAL N:M BROKER OPERATION: Register new VideoGeometryChangeClient
  // 
  // This method handles the "N" side of the N:M pattern by registering
  // VideoGeometryChangeClients that want to receive geometry updates.
  // 
  // CONCRETE CLIENTS that call this method:
  // - StarboardRendererClient (Cobalt video elements)
  // - CastRenderer (Chromecast video elements)  
  // - Multiple instances for multiple video elements on same page
  //
  // Each client is uniquely identified by overlay_plane_id to ensure
  // geometry updates are routed to the correct video element.
  
  // [ABHIJEET][PUNCH-OUT] Log client subscription for specific overlay plane
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] GEOMETRY SUBSCRIPTION ESTABLISHED"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Overlay Plane ID: " << overlay_plane_id
            << " | CLIENT: StarboardRendererClient (Renderer Process)"
            << " | PURPOSE: Client will receive geometry updates for this video plane"
            << " | Total Active Clients: " << (video_geometry_change_clients_.size() + 1);
            
  auto client = mojo::Remote<mojom::VideoGeometryChangeClient>(
      std::move(client_pending_remote));
  // The remote end closes the message pipe for the client when it no longer
  // wants to receive updates.
  // If the disconnect_handler is called, |this| must be alive, so Unretained is
  // safe.
  client.set_disconnect_handler(base::BindOnce(
      &VideoGeometrySetterService::OnVideoGeometryChangeClientGone,
      base::Unretained(this), overlay_plane_id));
  video_geometry_change_clients_[overlay_plane_id] = std::move(client);

  std::move(callback).Run();
}

void VideoGeometrySetterService::SetVideoGeometry(
    const gfx::RectF& rect_f,
    gfx::OverlayTransform transform,
    const base::UnguessableToken& overlay_plane_id) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  
  // CRITICAL N:M BROKER OPERATION: Forward geometry from setters to clients
  // 
  // This method handles the "M" side of the N:M pattern by receiving geometry 
  // updates from VideoGeometrySetters and forwarding to the correct client.
  //
  // CONCRETE SETTERS that call this method:
  // - OverlayStrategyUnderlayStarboard::CommitCandidate() (Cobalt compositor, GPU Process)
  // - OverlayStrategyUnderlayCast::CommitCandidate() (Chromecast compositor, GPU Process)
  //
  // BROKER FORWARDING LOGIC:
  // 1. Find VideoGeometryChangeClient for this overlay_plane_id
  // 2. Forward geometry update to that specific client
  // 3. Log warning if no client found (orphaned geometry update)
  //
  // This enables multiple compositor instances to update multiple video clients
  // while maintaining proper isolation and preventing cross-contamination.
  
  // [ABHIJEET][PUNCH-OUT] Log geometry update - this is the CRITICAL punch-out operation
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  auto video_geometry_change_client =
      video_geometry_change_clients_.find(overlay_plane_id);
  
  if (video_geometry_change_client != video_geometry_change_clients_.end()) {
    // ============================================================================
    // STEP 2 OF 4: BROWSER PROCESS → STARBOARDRENDERERCLIENT(RENDERER) FORWARDING
    // ============================================================================
    // 
    // VALIDATED 4-STEP GEOMETRY FLOW:
    // 1. [PREV] Compositor(GPU) → Browser Process (VideoGeometrySetterService) 
    // 2. [THIS STEP] Browser Process → StarboardRendererClient(Renderer Process)
    // 3. [NEXT] StarboardRendererClient(Renderer) → StarboardRenderer(GPU Process)  
    // 4. [FINAL] StarboardRenderer(GPU) → SbPlayer Platform (actual video positioning)
    //
    // This is the SECOND step where Browser Process VideoGeometrySetterService forwards 
    // geometry to the correct StarboardRendererClient based on overlay_plane_id mapping.
    
    LOG(INFO) << "[ABHIJEET][PUNCH-OUT] GEOMETRY UPDATE FORWARDED"
              << " | Process: " << process_name << " | PID: " << pid
              << " | Overlay Plane ID: " << overlay_plane_id
              << " | Geometry: " << rect_f.ToString()
              << " | Transform: " << static_cast<int>(transform)
              << " | STEP: 2/4 - Browser Process → StarboardRendererClient(Renderer)"
              << " | PURPOSE: N:M broker forwards geometry to correct video client";
              
    video_geometry_change_client->second->OnVideoGeometryChange(rect_f, transform);
  } else {
    LOG(WARNING) << "[ABHIJEET][PUNCH-OUT] GEOMETRY UPDATE DROPPED - NO CLIENT"
                 << " | Process: " << process_name << " | PID: " << pid
                 << " | Overlay Plane ID: " << overlay_plane_id
                 << " | Geometry: " << rect_f.ToString()
                 << " | Active Clients: " << video_geometry_change_clients_.size()
                 << " | ISSUE: No client subscribed for this overlay plane";
  }
}

// When a VideoGeometryChangeClient is gone, delete the corresponding entry in
// the mapping.
void VideoGeometrySetterService::OnVideoGeometryChangeClientGone(
    const base::UnguessableToken overlay_plane_id) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  
  // [ABHIJEET][PUNCH-OUT] Log client disconnection
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] GEOMETRY CLIENT DISCONNECTED"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Overlay Plane ID: " << overlay_plane_id
            << " | REASON: StarboardRendererClient (Renderer Process) disconnected"
            << " | Remaining Active Clients: " << (video_geometry_change_clients_.size() - 1)
            << " | PURPOSE: Cleanup - no more geometry updates for this video plane";
            
  video_geometry_change_clients_.erase(overlay_plane_id);
}

}  // namespace media
}  // namespace cobalt
