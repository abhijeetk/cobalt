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

#include "media/mojo/clients/starboard/starboard_renderer_client.h"

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/process/process.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "content/public/common/content_switches.h"
#include "media/base/media_log.h"
#include "media/base/media_resource.h"
#include "media/base/video_frame.h"
#include "media/mojo/clients/mojo_renderer.h"
#include "media/renderers/video_overlay_factory.h"
#include "media/video/gpu_video_accelerator_factories.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "ui/gfx/geometry/rect_conversions.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/task/bind_post_task.h"
#endif  // BUILDFLAG(IS_ANDROID)

namespace media {

StarboardRendererClient::StarboardRendererClient(
    const scoped_refptr<base::SequencedTaskRunner>& media_task_runner,
    std::unique_ptr<MediaLog> media_log,
    std::unique_ptr<MojoRenderer> mojo_renderer,
    std::unique_ptr<VideoOverlayFactory> video_overlay_factory,
    VideoRendererSink* video_renderer_sink,
    mojo::PendingRemote<RendererExtension> pending_renderer_extension,
    mojo::PendingReceiver<ClientExtension> client_extension_receiver,
    BindHostReceiverCallback bind_host_receiver_callback,
    GpuVideoAcceleratorFactories* gpu_factories
#if BUILDFLAG(IS_ANDROID)
    ,
    RequestOverlayInfoCB request_overlay_info_cb
#endif  // BUILDFLAG(IS_ANDROID)
    )
    : MojoRendererWrapper(std::move(mojo_renderer)),
      media_task_runner_(media_task_runner),
      media_log_(std::move(media_log)),
      video_overlay_factory_(std::move(video_overlay_factory)),
      video_renderer_sink_(video_renderer_sink),
      pending_renderer_extension_(std::move(pending_renderer_extension)),
      pending_client_extension_receiver_(std::move(client_extension_receiver)),
      client_extension_receiver_(this),
      bind_host_receiver_callback_(bind_host_receiver_callback),
      gpu_factories_(gpu_factories)
#if BUILDFLAG(IS_ANDROID)
      ,
      request_overlay_info_cb_(std::move(request_overlay_info_cb))
#endif  // BUILDFLAG(IS_ANDROID)
{
  DCHECK(media_task_runner_);
  DCHECK(video_renderer_sink_);
  DCHECK(video_overlay_factory_);
  DCHECK(bind_host_receiver_callback_);
  
  // [ABHIJEET][PUNCH-OUT] Log StarboardRendererClient creation - THE SUBSCRIBER AND VIDEO HOLE CREATOR
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] StarboardRendererClient CREATED - GEOMETRY SUBSCRIBER & VIDEO HOLE CREATOR"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName()
            << " | LOCATION: Media Thread in Renderer Process"
            << " | Overlay Plane ID: " << video_overlay_factory_->overlay_plane_id()
            << " | PURPOSE: Creates video holes and subscribes to geometry updates";
            
  // CLIENT ARCHITECTURE DOCUMENTATION:
  // StarboardRendererClient is the SUBSCRIBER that:
  // - Lives in Media Thread in Renderer Process 
  // - Subscribes to VideoGeometrySetterService in Browser Process
  // - Creates video holes (VideoHoleDrawQuad) via VideoRendererSink
  // - Communicates with StarboardRenderer in GPU Process via Mojo
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] CLIENT ARCHITECTURE:"
            << " | THIS CLIENT: Renderer Process Media Thread"
            << " | SUBSCRIBES TO: VideoGeometrySetterService in Browser Process"
            << " | CREATES: Video holes (VideoHoleDrawQuad) via VideoRendererSink"
            << " | COMMUNICATES WITH: StarboardRenderer in GPU Process via Mojo IPC";
}

StarboardRendererClient::~StarboardRendererClient() {
  SetPlayingState(false);
  DCHECK(!video_renderer_sink_started_);

#if BUILDFLAG(IS_ANDROID)
  if (request_overlay_info_cb_ && overlay_info_requested_) {
    request_overlay_info_cb_.Run(false, base::NullCallback());
    overlay_info_requested_ = false;
  }
#endif  // BUILDFLAG(IS_ANDROID)
}

void StarboardRendererClient::Initialize(MediaResource* media_resource,
                                         RendererClient* client,
                                         PipelineStatusCallback init_cb) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(!init_cb_);

  client_ = client;
  init_cb_ = std::move(init_cb);

  // [ABHIJEET][PUNCH-OUT] CRITICAL GEOMETRY SUBSCRIPTION - THE SUBSCRIBER SETUP
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] GEOMETRY SUBSCRIPTION SETUP"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName()
            << " | Overlay Plane ID: " << video_overlay_factory_->overlay_plane_id()
            << " | PURPOSE: Setting up subscription to receive geometry updates from Browser Process";
  
  // Bind the receiver of VideoGeometryChangeSubscriber on renderer Thread.
  // This uses BindPostTaskToCurrentDefault() to ensure the callback is ran
  // on renderer thread, not media thread.
  bind_host_receiver_callback_.Run(
      video_geometry_change_subcriber_remote_.BindNewPipeAndPassReceiver());
  DCHECK(video_geometry_change_subcriber_remote_);
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] SUBSCRIBING TO GEOMETRY CHANGES"
            << " | HOST: VideoGeometrySetterService in Browser Process"
            << " | CLIENT: THIS StarboardRendererClient (Media Thread)"
            << " | PURPOSE: Will receive geometry updates for punch-out video hole positioning";
            
  video_geometry_change_subcriber_remote_->SubscribeToVideoGeometryChange(
      video_overlay_factory_->overlay_plane_id(),
      video_geometry_change_client_receiver_.BindNewPipeAndPassRemote(),
      base::BindOnce(&StarboardRendererClient::OnSubscribeToVideoGeometryChange,
                     base::Unretained(this), media_resource, client));

  DCHECK(!AreMojoPipesConnected());
  InitAndBindMojoRenderer(base::BindOnce(
      &StarboardRendererClient::InitializeMojoRenderer,
      weak_factory_.GetWeakPtr(), media_resource, client,
      base::BindOnce(&StarboardRendererClient::OnMojoRendererInitialized,
                     weak_factory_.GetWeakPtr())));
}

void StarboardRendererClient::StartPlayingFrom(base::TimeDelta time) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  DCHECK_NE(rendering_mode_, StarboardRenderingMode::kInvalid);
  SetPlayingState(true);
  {
    base::AutoLock auto_lock(lock_);
    next_video_frame_.reset();
  }
  MojoRendererWrapper::StartPlayingFrom(time);
}

RendererType StarboardRendererClient::GetRendererType() {
  return RendererType::kStarboard;
}

void StarboardRendererClient::OnError(PipelineStatus status) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  SetPlayingState(false);
  client_->OnError(status);
}

void StarboardRendererClient::OnFallback(PipelineStatus fallback) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  SetPlayingState(false);
  client_->OnFallback(std::move(fallback).AddHere());
}

void StarboardRendererClient::OnEnded() {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  SetPlayingState(false);
  client_->OnEnded();
}

void StarboardRendererClient::OnStatisticsUpdate(
    const PipelineStatistics& stats) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  client_->OnStatisticsUpdate(stats);
}

void StarboardRendererClient::OnBufferingStateChange(
    BufferingState state,
    BufferingStateChangeReason reason) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  client_->OnBufferingStateChange(state, reason);
}

void StarboardRendererClient::OnWaiting(WaitingReason reason) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  client_->OnWaiting(reason);
}

void StarboardRendererClient::OnAudioConfigChange(
    const AudioDecoderConfig& config) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  client_->OnAudioConfigChange(config);
}

void StarboardRendererClient::OnVideoConfigChange(
    const VideoDecoderConfig& config) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  client_->OnVideoConfigChange(config);
}

void StarboardRendererClient::OnVideoNaturalSizeChange(const gfx::Size& size) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  // PaintVideoHoleFrame() is called on StarboardRenderer after
  // OnVideoNaturalSizeChange(), so we don't need to refresh
  // |video_renderer_sink_| here.
  client_->OnVideoNaturalSizeChange(size);
}

void StarboardRendererClient::OnVideoOpacityChange(bool opaque) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  client_->OnVideoOpacityChange(opaque);
}

void StarboardRendererClient::OnVideoFrameRateChange(absl::optional<int> fps) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  client_->OnVideoFrameRateChange(fps);
}

scoped_refptr<VideoFrame> StarboardRendererClient::Render(
    base::TimeTicks deadline_min,
    base::TimeTicks deadline_max,
    VideoRendererSink::RenderCallback::RenderingMode rendering_mode) {
  // This is called on VideoFrameCompositor thread.
  DCHECK_EQ(rendering_mode_, StarboardRenderingMode::kDecodeToTexture);
  DCHECK(!media_task_runner_->RunsTasksInCurrentSequence());
  
  // [ABHIJEET][PUNCH-OUT] DECODE-TO-TEXTURE RENDER PATH - CALLBACK-DRIVEN VIDEO RENDERING
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] StarboardRendererClient::Render - DECODE-TO-TEXTURE CALLBACK"
            << " | Thread: " << base::PlatformThread::GetName()
            << " | Mode: DECODE_TO_TEXTURE (confirmed via DCHECK)"
            << " | Deadline Min: " << deadline_min.ToInternalValue()
            << " | Deadline Max: " << deadline_max.ToInternalValue()
            << " | PURPOSE: VideoFrameCompositor requesting video frame for texture-based rendering";
  media_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&StarboardRendererClient::UpdateCurrentFrame,
                                weak_factory_.GetWeakPtr()));
  // TODO(b/422524413): investigate the impact of delay frames for a/v sync.
  scoped_refptr<VideoFrame> frame_to_render;
  {
    base::AutoLock auto_lock(lock_);
    frame_to_render = next_video_frame_;
  }
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] StarboardRendererClient::Render - RETURNING VIDEO FRAME"
            << " | Frame: " << (frame_to_render ? "VALID" : "NULL")
            << " | Format: " << (frame_to_render ? static_cast<int>(frame_to_render->format()) : -1)
            << " | Size: " << (frame_to_render ? frame_to_render->natural_size().ToString() : "NULL")
            << " | PURPOSE: VideoFrame returned to VideoFrameCompositor for texture-based rendering";
            
  return frame_to_render;
}

void StarboardRendererClient::OnFrameDropped() {
  // This is called on VideoFrameCompositor thread.
  // no-op: dropped frame is handled by SbPlayer.
  // TODO(b/422527806): investigate to report dropped frame.
}

base::TimeDelta StarboardRendererClient::GetPreferredRenderInterval() {
  // This is at 60fps for render interval and called on
  // VideoFrameCompositor thread.
  return base::Microseconds(16666);
}

void StarboardRendererClient::PaintVideoHoleFrame(const gfx::Size& size) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  
  // [ABHIJEET][PUNCH-OUT] CRITICAL VIDEO HOLE CREATION - THE ACTUAL HOLE CREATION
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] CREATING VIDEO HOLE FRAME - THE ACTUAL PUNCH-OUT HOLE"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName()
            << " | Video Size: " << size.ToString()
            << " | Width: " << size.width() << " | Height: " << size.height()
            << " | Overlay Plane ID: " << video_overlay_factory_->overlay_plane_id()
            << " | PURPOSE: Creating VideoHoleDrawQuad for punch-out video rendering";
  
  // This could be called from StarboardRenderer
  // before UpdateStarboardRenderingMode(), so this is not
  // required |rendering_mode_| equals to StarboardRenderingMode::kPunchOut.
  
  // VIDEO HOLE CREATION PROCESS:
  // 1. video_overlay_factory_->CreateFrame() creates VideoHoleDrawQuad
  // 2. video_renderer_sink_->PaintSingleFrame() sends it to compositor
  // 3. Compositor creates transparent hole for hardware video overlay
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] VIDEO HOLE CREATION FLOW:"
            << " | STEP 1: VideoOverlayFactory->CreateFrame() creates VideoHoleDrawQuad"
            << " | STEP 2: VideoRendererSink->PaintSingleFrame() sends to compositor"
            << " | STEP 3: Compositor creates transparent hole for hardware overlay"
            << " | RESULT: Transparent hole created in UI for video content";
            
  // [ABHIJEET][PUNCH-OUT] CRITICAL: VideoFrame creation and sink forwarding
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] CREATING AND FORWARDING VIDEO HOLE FRAME\""
            << " | Creating VideoFrame: video_overlay_factory_->CreateFrame(" << size.ToString() << ")"
            << " | Overlay Plane ID: " << video_overlay_factory_->overlay_plane_id()
            << " | PURPOSE: Creating VideoHoleDrawQuad and sending to VideoRendererSink";
            
  scoped_refptr<VideoFrame> hole_frame = video_overlay_factory_->CreateFrame(size);
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] VIDEO HOLE FRAME CREATED - FORWARDING TO COMPOSITOR\""
            << " | VideoFrame Created: " << (hole_frame ? "SUCCESS" : "FAILED")
            << " | VideoFrame Format: " << (hole_frame ? static_cast<int>(hole_frame->format()) : -1)
            << " | Sending to: video_renderer_sink_->PaintSingleFrame()"
            << " | PURPOSE: VideoRendererSink will forward this VideoHoleDrawQuad to compositor";
            
  video_renderer_sink_->PaintSingleFrame(hole_frame);
      
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] VIDEO HOLE FRAME PAINTED"
            << " | VideoHoleDrawQuad sent to compositor"
            << " | Punch-out hole created for hardware video overlay";
}

void StarboardRendererClient::UpdateStarboardRenderingMode(
    const StarboardRenderingMode mode) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  
  // [ABHIJEET][PUNCH-OUT] CRITICAL BRANCHING POINT - DECODE-TO-TEXTURE vs PUNCH-OUT
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  std::string mode_str;
  switch (mode) {
    case StarboardRenderingMode::kDecodeToTexture:
      mode_str = "DECODE_TO_TEXTURE";
      break;
    case StarboardRenderingMode::kPunchOut:
      mode_str = "PUNCH_OUT";
      break;
    case StarboardRenderingMode::kInvalid:
      mode_str = "INVALID";
      break;
  }
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] UpdateStarboardRenderingMode - RENDERING PATH DECISION"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName()
            << " | Previous Mode: " << static_cast<int>(rendering_mode_)
            << " | NEW MODE: " << mode_str << " (" << static_cast<int>(mode) << ")"
            << " | PURPOSE: CRITICAL BRANCHING POINT - determines decode-to-texture vs punch-out rendering path";
  
  rendering_mode_ = mode;
  switch (rendering_mode_) {
    case StarboardRenderingMode::kPunchOut:
      // StarboardRenderingMode::kPunchOut doesn't update video
      // frame via VideoRendererSink::RenderCallback::Render().
      // The video frame is handled by Sbplayer, and render to its
      // surface directly.
      LOG(INFO) << "[ABHIJEET][PUNCH-OUT] PUNCH-OUT MODE ACTIVATED"
                << " | Path: Video handled by SbPlayer directly (hardware overlay)"
                << " | VideoRendererSink: " << (is_playing_ ? "STOPPING" : "ALREADY_STOPPED")
                << " | Video rendering: Direct to hardware surface (bypass compositor)"
                << " | PaintVideoHoleFrame: Creates transparent holes for overlays"
                << " | PURPOSE: Hardware-accelerated video with compositor holes";
      if (is_playing_) {
        StopVideoRendererSink();
      } else {
        LOG(WARNING) << "StarboardRendererClient doesn't stop video rendering "
                        "sink, since the video is not playing.";
      }
      break;
    case StarboardRenderingMode::kDecodeToTexture:
      // StarboardRenderingMode::kDecodeToTexture needs to update
      // video frame via VideoRendererSink::RenderCallback::Render().
      LOG(INFO) << "[ABHIJEET][PUNCH-OUT] DECODE-TO-TEXTURE MODE ACTIVATED"
                << " | Path: Video handled via VideoRendererSink callbacks"
                << " | VideoRendererSink: " << (is_playing_ ? "STARTING" : "WILL_START_ON_PLAY")
                << " | Video rendering: Through compositor (CPU/GPU textures)"
                << " | Render(): Called periodically to get video frames"
                << " | PURPOSE: Software/GPU texture-based video rendering through compositor";
      if (is_playing_) {
        StartVideoRendererSink();
      } else {
        LOG(WARNING) << "StarboardRendererClient doesn't start video rendering "
                        "sink, since StartPlayingFrom() is not called.";
      }
      break;
    case StarboardRenderingMode::kInvalid:
      NOTREACHED() << "Invalid SbPlayer output mode";
      break;
  }
  // OnMojoRendererInitialized() should be called from StarboardRenderer
  // after this. In the case where OnMojoRendererInitialized() is called
  // before this, run |init_cb_| if not null.
  if (IsMojoRendererInitialized() && !init_cb_.is_null()) {
    std::move(init_cb_).Run(pipeline_status());
  }
}

#if BUILDFLAG(IS_ANDROID)
void StarboardRendererClient::RequestOverlayInfo(bool restart_for_transitions) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(request_overlay_info_cb_);

  overlay_info_requested_ = true;
  request_overlay_info_cb_.Run(
      restart_for_transitions,
      base::BindPostTaskToCurrentDefault(
          base::BindRepeating(&StarboardRendererClient::OnOverlayInfoChanged,
                              weak_factory_.GetWeakPtr())));
}
#endif  // BUILDFLAG(IS_ANDROID)

void StarboardRendererClient::OnVideoGeometryChange(
    const gfx::RectF& rect_f,
    gfx::OverlayTransform /* transform */) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  
  // [ABHIJEET][PUNCH-OUT] GEOMETRY UPDATE RECEIVED - THE SUBSCRIBER RECEIVES UPDATE
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  gfx::Rect new_bounds = gfx::ToEnclosingRect(rect_f);
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] GEOMETRY UPDATE RECEIVED BY SUBSCRIBER"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName()
            << " | Source Rect: " << rect_f.ToString()
            << " | New Bounds: " << new_bounds.ToString()
            << " | X: " << new_bounds.x() << " | Y: " << new_bounds.y()
            << " | Width: " << new_bounds.width() << " | Height: " << new_bounds.height()
            << " | PURPOSE: Forwarding geometry update to StarboardRenderer in GPU process";
            
  // GEOMETRY UPDATE FLOW:
  // 1. Received from VideoGeometrySetterService (Browser Process)
  // 2. Forward to StarboardRenderer (GPU Process) via Mojo
  // 3. StarboardRenderer will update SbPlayerSetBounds
  // ============================================================================
  // STEP 3 OF 4: STARBOARDRENDERERCLIENT(RENDERER) → STARBOARDRENDERER(GPU) FORWARDING
  // ============================================================================
  // 
  // VALIDATED 4-STEP GEOMETRY FLOW:
  // 1. [PREV] Compositor(GPU) → Browser Process (VideoGeometrySetterService)
  // 2. [PREV] Browser Process → StarboardRendererClient(Renderer Process)
  // 3. [THIS STEP] StarboardRendererClient(Renderer) → StarboardRenderer(GPU Process)
  // 4. [FINAL] StarboardRenderer(GPU) → SbPlayer Platform (actual video positioning)
  //
  // This is the THIRD step where StarboardRendererClient in Renderer Process forwards
  // geometry updates to StarboardRenderer in GPU Process via direct Mojo pipe.
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] FORWARDING GEOMETRY TO GPU PROCESS"
            << " | FROM: VideoGeometrySetterService (Browser Process)"
            << " | TO: StarboardRenderer (GPU Process) via renderer_extension_"
            << " | STEP: 3/4 - StarboardRendererClient(Renderer) → StarboardRenderer(GPU)"
            << " | PURPOSE: Direct Renderer→GPU communication for final positioning";
            
  renderer_extension_->OnVideoGeometryChange(new_bounds);
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] GEOMETRY UPDATE FORWARDED TO STARBOARD RENDERER"
            << " | NEXT: StarboardRenderer will call SbPlayerSetBounds for platform positioning";
}

void StarboardRendererClient::OnConnectionError() {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  MEDIA_LOG(ERROR, media_log_) << "StarboardRendererClient disconnected";
  client_->OnError(PIPELINE_ERROR_DISCONNECTED);
}

void StarboardRendererClient::OnSubscribeToVideoGeometryChange(
    MediaResource* media_resource,
    RendererClient* client) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
}

void StarboardRendererClient::InitAndBindMojoRenderer(
    base::OnceClosure complete_cb) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(!AreMojoPipesConnected());

  // Consume and bind the delayed PendingRemote and PendingReceiver now that
  // we are on |media_task_runner_|.
  renderer_extension_.Bind(std::move(pending_renderer_extension_),
                           media_task_runner_);
  client_extension_receiver_.Bind(std::move(pending_client_extension_receiver_),
                                  media_task_runner_);

  renderer_extension_.set_disconnect_handler(base::BindOnce(
      &StarboardRendererClient::OnConnectionError, base::Unretained(this)));

  // Generate |command_buffer_id|.
  mojom::CommandBufferIdPtr command_buffer_id;
  if (gpu_factories_) {
    gpu_factories_->GetChannelToken(
        base::BindOnce(&StarboardRendererClient::OnGpuChannelTokenReady,
                       weak_factory_.GetWeakPtr(), std::move(command_buffer_id),
                       std::move(complete_cb)));
    return;
  }

  DCHECK(complete_cb);
  InitAndConstructMojoRenderer(std::move(command_buffer_id),
                               std::move(complete_cb));
}

void StarboardRendererClient::OnGpuChannelTokenReady(
    mojom::CommandBufferIdPtr command_buffer_id,
    base::OnceClosure complete_cb,
    const base::UnguessableToken& channel_token) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  if (channel_token) {
    command_buffer_id = mojom::CommandBufferId::New();
    command_buffer_id->channel_token = std::move(channel_token);
    command_buffer_id->route_id = gpu_factories_->GetCommandBufferRouteId();
  }
  InitAndConstructMojoRenderer(std::move(command_buffer_id),
                               std::move(complete_cb));
}

void StarboardRendererClient::InitializeMojoRenderer(
    MediaResource* media_resource,
    RendererClient* client,
    PipelineStatusCallback init_cb) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(AreMojoPipesConnected());
  MojoRendererWrapper::Initialize(media_resource, client, std::move(init_cb));
}

void StarboardRendererClient::InitAndConstructMojoRenderer(
    media::mojom::CommandBufferIdPtr command_buffer_id,
    base::OnceClosure complete_cb) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(AreMojoPipesConnected());
  // Notify GpuChannelToken to StarboardRendererWrapper before
  // MojoRendererWrapper::Initialize(). Hence, StarboardRendererWrapper
  // should have |command_buffer_id| if available before
  // StarboardRendererWrapper::Initialize().
  renderer_extension_->OnGpuChannelTokenReady(std::move(command_buffer_id));
  std::move(complete_cb).Run();
}

void StarboardRendererClient::OnMojoRendererInitialized(PipelineStatus status) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  if (rendering_mode_ != StarboardRenderingMode::kInvalid) {
    DCHECK(!init_cb_.is_null());
    std::move(init_cb_).Run(status);
  }
  SetMojoRendererInitialized(status);

  // StarboardRenderer reports |rendering_mode_| before calling
  // OnMojoRendererInitialized(). If |rendering_mode_| is not
  // reported yet, call |init_cb_| in UpdateStarboardRenderingMode()
  // to inform media pipeline.
}

void StarboardRendererClient::SetMojoRendererInitialized(
    PipelineStatus status) {
  base::AutoLock auto_lock(lock_);
  is_mojo_renderer_initialized_ = true;
  pipeline_status_ = status;
}

bool StarboardRendererClient::IsMojoRendererInitialized() const {
  base::AutoLock auto_lock(lock_);
  return is_mojo_renderer_initialized_;
}

PipelineStatus StarboardRendererClient::pipeline_status() const {
  base::AutoLock auto_lock(lock_);
  return pipeline_status_;
}

void StarboardRendererClient::SetPlayingState(bool is_playing) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  // Skip if we are already in the same playing state.
  if (is_playing == is_playing_) {
    return;
  }

  // Only start the render loop if we are in decode-to-texture mode.
  if (rendering_mode_ == StarboardRenderingMode::kDecodeToTexture) {
    if (is_playing) {
      StartVideoRendererSink();
    } else {
      StopVideoRendererSink();
    }
  }
  is_playing_ = is_playing;
}

void StarboardRendererClient::UpdateCurrentFrame() {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(rendering_mode_, StarboardRenderingMode::kDecodeToTexture);
  renderer_extension_->GetCurrentVideoFrame(
      base::BindOnce(&StarboardRendererClient::OnGetCurrentVideoFrameDone,
                     weak_factory_.GetWeakPtr()));
}

void StarboardRendererClient::OnGetCurrentVideoFrameDone(
    const scoped_refptr<VideoFrame>& frame) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  if (frame) {
    base::AutoLock auto_lock(lock_);
    next_video_frame_ = std::move(frame);
  }
}

#if BUILDFLAG(IS_ANDROID)
void StarboardRendererClient::OnOverlayInfoChanged(
    const OverlayInfo& overlay_info) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  renderer_extension_->OnOverlayInfoChanged(overlay_info);
}
#endif  // BUILDFLAG(IS_ANDROID)

void StarboardRendererClient::StartVideoRendererSink() {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  if (!video_renderer_sink_started_) {
    video_renderer_sink_started_ = true;
    video_renderer_sink_->Start(this);
  }
}

void StarboardRendererClient::StopVideoRendererSink() {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  if (video_renderer_sink_started_) {
    video_renderer_sink_started_ = false;
    video_renderer_sink_->Stop();
  }
}

}  // namespace media
