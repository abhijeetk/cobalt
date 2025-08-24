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

#include "media/mojo/clients/starboard/starboard_renderer_client_factory.h"

#include "base/check.h"
#include "base/command_line.h"
#include "base/process/current_process.h"
#include "base/process/process.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/threading/platform_thread.h"
#include "content/public/common/content_switches.h"
#include "media/base/media_switches.h"
#include "media/base/starboard/starboard_renderer_config.h"
#include "media/mojo/clients/mojo_media_log_service.h"
#include "media/mojo/clients/mojo_renderer.h"
#include "media/mojo/clients/mojo_renderer_factory.h"
#include "media/mojo/clients/mojo_renderer_wrapper.h"
#include "media/mojo/clients/starboard/starboard_renderer_client.h"
#include "media/mojo/mojom/renderer_extensions.mojom.h"
#include "media/renderers/video_overlay_factory.h"
#include "media/video/gpu_video_accelerator_factories.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"

namespace media {

StarboardRendererClientFactory::StarboardRendererClientFactory(
    MediaLog* media_log,
    std::unique_ptr<MojoRendererFactory> mojo_renderer_factory,
    const GetGpuFactoriesCB& get_gpu_factories_cb,
    const media::RendererFactoryTraits* traits)
    : media_log_(media_log),
      mojo_renderer_factory_(std::move(mojo_renderer_factory)),
      get_gpu_factories_cb_(get_gpu_factories_cb),
      audio_write_duration_local_(
          base::FeatureList::IsEnabled(kCobaltAudioWriteDuration)
              ? kAudioWriteDurationLocal.Get()
              : traits->audio_write_duration_local),
      audio_write_duration_remote_(
          base::FeatureList::IsEnabled(kCobaltAudioWriteDuration)
              ? kAudioWriteDurationRemote.Get()
              : traits->audio_write_duration_remote),
      max_video_capabilities_(traits->max_video_capabilities),
      viewport_size_(traits->viewport_size),
      bind_host_receiver_callback_(traits->bind_host_receiver_callback) {
  
  // [ABHIJEET][PUNCH-OUT] Log StarboardRendererClientFactory creation with detailed IPC binding info
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] StarboardRendererClientFactory CREATED"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName();
            
  // IPC BINDING MECHANISM DOCUMENTATION:
  // bind_host_receiver_callback_ is a cross-process binding callback that:
  // - HOST: Browser Process (where VideoGeometrySetterService lives)
  // - RECEIVER: This Renderer Process (where StarboardRendererClient lives)  
  // - CONNECTION: Renderer → Browser Process for video geometry subscription services
  // - PURPOSE: Allows StarboardRendererClient to subscribe to VideoGeometrySetterService in Browser Process
  // - NOTE: This is SEPARATE from direct Renderer ↔ GPU communication pipes
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] IPC BINDING CONFIG:"
            << " | bind_host_receiver_callback_: " << (bind_host_receiver_callback_ ? "SET" : "NULL")
            << " | BINDING FLOW: Renderer Process (THIS=" << process_name << ") → Browser Process (HOST)"
            << " | PURPOSE: Cross-process video geometry subscription service binding for punch-out";
}

StarboardRendererClientFactory::~StarboardRendererClientFactory() = default;

std::unique_ptr<Renderer> StarboardRendererClientFactory::CreateRenderer(
    const scoped_refptr<base::SequencedTaskRunner>& media_task_runner,
    const scoped_refptr<base::TaskRunner>& /*worker_task_runner*/,
    AudioRendererSink* /*audio_renderer_sink*/,
    VideoRendererSink* video_renderer_sink,
    RequestOverlayInfoCB request_overlay_info_cb,
    const gfx::ColorSpace& /*target_color_space*/) {
#if BUILDFLAG(IS_ANDROID)
  DCHECK(request_overlay_info_cb);
#endif  // BUILDFLAG(IS_ANDROID)
  DCHECK(video_renderer_sink);
  DCHECK(media_log_);
  DCHECK(mojo_renderer_factory_);
  DCHECK(bind_host_receiver_callback_);

  // [ABHIJEET][PUNCH-OUT] Log CreateRenderer start with process/thread info
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] CreateRenderer STARTED"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName()
            << " | About to create Mojo IPC pipes for punch-out video rendering";

  // [ABHIJEET][PUNCH-OUT] MOJO IPC PIPE 1: MediaLog Service
  // HOST: This Renderer Process | RECEIVER: This Renderer Process (self-owned)
  // PURPOSE: Local media logging service for StarboardRenderer messages
  mojo::PendingReceiver<mojom::MediaLog> media_log_pending_receiver;
  auto media_log_pending_remote =
      media_log_pending_receiver.InitWithNewPipeAndPassRemote();
  mojo::MakeSelfOwnedReceiver(
      std::make_unique<MojoMediaLogService>(media_log_->Clone()),
      std::move(media_log_pending_receiver));
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] MOJO PIPE 1 CREATED: MediaLog"
            << " | HOST: Renderer Process (THIS=" << process_name << ")"
            << " | RECEIVER: Self-owned MojoMediaLogService"
            << " | PURPOSE: Local media logging for StarboardRenderer";

  // [ABHIJEET][PUNCH-OUT] MOJO IPC PIPE 2: StarboardRendererExtension  
  // HOST: GPU Process (StarboardRenderer) | RECEIVER: GPU Process
  // PURPOSE: Renderer Process → GPU Process commands (geometry, overlays)
  // Used to send messages from the StarboardRendererClient (Media thread in
  // Chrome_InProcRendererThread), to the StarboardRenderer (PooledSingleThread
  // in Chrome_InProcGpuThread). The |renderer_extension_receiver| will be bound
  // in StarboardRenderer.
  mojo::PendingRemote<media::mojom::StarboardRendererExtension>
      renderer_extension_remote;
  auto renderer_extension_receiver =
      renderer_extension_remote.InitWithNewPipeAndPassReceiver();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] MOJO PIPE 2 CREATED: StarboardRendererExtension"
            << " | HOST: GPU Process (StarboardRenderer)"
            << " | RECEIVER: GPU Process (StarboardRenderer)"
            << " | PURPOSE: Renderer Process → GPU Process (video geometry, overlay commands)";

  // [ABHIJEET][PUNCH-OUT] MOJO IPC PIPE 3: StarboardRendererClientExtension
  // HOST: Renderer Process (StarboardRendererClient) | RECEIVER: Renderer Process  
  // PURPOSE: GPU Process → Renderer Process callbacks (status, events)
  // Used to send messages from the StarboardRenderer (PooledSingleThread in
  // Chrome_InProcGpuThread), to the StarboardRendererClient (Media thread in
  // Chrome_InProcRendererThread).
  mojo::PendingRemote<mojom::StarboardRendererClientExtension>
      client_extension_remote;
  auto client_extension_receiver =
      client_extension_remote.InitWithNewPipeAndPassReceiver();
      
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] MOJO PIPE 3 CREATED: StarboardRendererClientExtension"
            << " | HOST: Renderer Process (StarboardRendererClient)"
            << " | RECEIVER: Renderer Process (StarboardRendererClient)" 
            << " | PURPOSE: GPU Process → Renderer Process (status callbacks, events)";

  // [ABHIJEET][PUNCH-OUT] Create VideoOverlayFactory for punch-out video frames
  // This factory manages video overlay planes for hardware-accelerated rendering
  auto overlay_factory = std::make_unique<VideoOverlayFactory>();
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] VideoOverlayFactory CREATED"
            << " | Overlay Plane ID: " << overlay_factory->overlay_plane_id()
            << " | PURPOSE: Manages video overlay planes for punch-out rendering";

  // [ABHIJEET][PUNCH-OUT] Get GPU factories for decode-to-texture fallback support
  // GetChannelToken() from gpu::GpuChannel for StarboardRendererClient.
  DCHECK(get_gpu_factories_cb_);
  GpuVideoAcceleratorFactories* gpu_factories = get_gpu_factories_cb_.Run();
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] GPU Factories OBTAINED"
            << " | GPU Factories: " << (gpu_factories ? "AVAILABLE" : "NULL")
            << " | PURPOSE: Supports decode-to-texture mode when punch-out unavailable";

  // [ABHIJEET][PUNCH-OUT] Initialize StarboardRendererConfig for cross-process renderer
  StarboardRendererConfig config(
      overlay_factory->overlay_plane_id(), audio_write_duration_local_,
      audio_write_duration_remote_, max_video_capabilities_, viewport_size_);
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] StarboardRendererConfig CREATED"
            << " | Max Video Capabilities: " << max_video_capabilities_
            << " | Viewport Size: " << viewport_size_.ToString()
            << " | PURPOSE: Configuration for remote StarboardRenderer in GPU process";
            
  // [ABHIJEET][PUNCH-OUT] Create MojoRenderer - this establishes the cross-process connection
  std::unique_ptr<media::MojoRenderer> mojo_renderer =
      mojo_renderer_factory_->CreateStarboardRenderer(
          std::move(media_log_pending_remote), config,
          std::move(renderer_extension_receiver),
          std::move(client_extension_remote), media_task_runner,
          video_renderer_sink);
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] MojoRenderer CREATED via MojoRendererFactory"
            << " | TARGET: StarboardRenderer in GPU Process"
            << " | CONNECTION: Established Renderer Process → GPU Process"
            << " | PIPES TRANSFERRED: MediaLog, RendererExtension, ClientExtension";

  // [ABHIJEET][PUNCH-OUT] Create StarboardRendererClient with bind_host_receiver_callback_
  // CRITICAL IPC BINDING POINT: bind_host_receiver_callback_ enables cross-process service binding
  // - THIS CLIENT (Renderer Process) calls bind_host_receiver_callback_  
  // - CALLBACK connects to VideoGeometrySetterService in Browser Process
  // - Enables subscription to video geometry updates from Browser Process hub
  // - NOTE: Direct Renderer ↔ GPU communication uses separate Mojo pipes (created above)
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] Creating StarboardRendererClient with IPC BINDING"
            << " | bind_host_receiver_callback_: " << (bind_host_receiver_callback_ ? "WILL_BIND" : "NULL")
            << " | IPC FLOW: StarboardRendererClient → bind_host_receiver_callback_ → Browser Process Services"
            << " | PURPOSE: Enable cross-process video geometry subscription service binding";

  return std::make_unique<media::StarboardRendererClient>(
      media_task_runner, media_log_->Clone(), std::move(mojo_renderer),
      std::move(overlay_factory), video_renderer_sink,
      std::move(renderer_extension_remote),
      std::move(client_extension_receiver), bind_host_receiver_callback_,
      gpu_factories
#if BUILDFLAG(IS_ANDROID)
      ,
      std::move(request_overlay_info_cb)
#endif  // BUILDFLAG(IS_ANDROID)
  );
}

}  // namespace media
