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

#include "cobalt/gpu/cobalt_content_gpu_client.h"

#include "base/memory/ptr_util.h"
#include "base/task/single_thread_task_runner.h"
#include "components/viz/service/display/starboard/overlay_strategy_underlay_starboard.h"
#include "content/public/child/child_thread.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/process/process.h"
#include "base/threading/platform_thread.h"
#include "content/public/common/content_switches.h"

namespace cobalt {

CobaltContentGpuClient::CobaltContentGpuClient() = default;
CobaltContentGpuClient::~CobaltContentGpuClient() = default;

void CobaltContentGpuClient::PostCompositorThreadCreated(
    base::SingleThreadTaskRunner* task_runner) {
  // [ABHIJEET][PUNCH-OUT] Log GPU process setting up Compositor → Browser connection
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] CobaltContentGpuClient::PostCompositorThreadCreated - SETTING UP COMPOSITOR CONNECTION"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName()
            << " | PURPOSE: GPU Process setting up VideoGeometrySetter connection for Compositor";
  
  // Initialize PendingRemote for VideoGeometrySetter and post it
  // to compositor thread (viz service). This is called on gpu thread
  // right after the compositor thread is created.
  mojo::PendingRemote<cobalt::media::mojom::VideoGeometrySetter>
      video_geometry_setter;
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] CobaltContentGpuClient::PostCompositorThreadCreated - BINDING HOST RECEIVER"
            << " | PURPOSE: Creating Mojo pipe to Browser Process VideoGeometrySetterService";
  
  content::ChildThread::Get()->BindHostReceiver(
      video_geometry_setter.InitWithNewPipeAndPassReceiver());

  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] CobaltContentGpuClient::PostCompositorThreadCreated - POSTING TO COMPOSITOR THREAD"
            << " | PURPOSE: Sending VideoGeometrySetter connection to Compositor thread";
  
  task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          &viz::OverlayStrategyUnderlayStarboard::ConnectVideoGeometrySetter,
          std::move(video_geometry_setter)));
}

}  // namespace cobalt
