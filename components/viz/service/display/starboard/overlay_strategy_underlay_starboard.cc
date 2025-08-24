// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/service/display/starboard/overlay_strategy_underlay_starboard.h"

#include <utility>
#include <vector>

#include "base/containers/adapters.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/unguessable_token.h"
#include "components/viz/common/quads/draw_quad.h"
#include "components/viz/common/quads/debug_border_draw_quad.h"
#include "components/viz/common/quads/solid_color_draw_quad.h"
#include "components/viz/common/quads/video_hole_draw_quad.h"
#include "third_party/skia/include/core/SkColor.h"
#include "components/viz/service/display/overlay_candidate_factory.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "base/command_line.h"
#include "base/process/process.h"
#include "base/threading/platform_thread.h"
#include "content/public/common/content_switches.h"

namespace viz {

namespace {

// This persistent mojo::Remote is bound then used by all the instances
// of OverlayStrategyUnderlayStarboard.
mojo::Remote<cobalt::media::mojom::VideoGeometrySetter>&
GetVideoGeometrySetter() {
  static base::NoDestructor<
      mojo::Remote<cobalt::media::mojom::VideoGeometrySetter>>
      g_video_geometry_setter;
  return *g_video_geometry_setter;
}

}  // namespace

OverlayStrategyUnderlayStarboard::OverlayStrategyUnderlayStarboard(
    OverlayProcessorUsingStrategy* capability_checker)
    : OverlayStrategyUnderlay(capability_checker) {
  // [ABHIJEET][PUNCH-OUT] Log OverlayStrategyUnderlayStarboard creation - COMPOSITOR THREAD
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] OverlayStrategyUnderlayStarboard CREATED"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName()
            << " | PURPOSE: Compositor strategy for handling video hole overlays";
}

OverlayStrategyUnderlayStarboard::~OverlayStrategyUnderlayStarboard() {}

void OverlayStrategyUnderlayStarboard::Propose(
    const SkM44& output_color_matrix,
    const OverlayProcessorInterface::FilterOperationsMap& render_pass_filters,
    const OverlayProcessorInterface::FilterOperationsMap&
        render_pass_backdrop_filters,
    DisplayResourceProvider* resource_provider,
    AggregatedRenderPassList* render_pass_list,
    SurfaceDamageRectList* surface_damage_rect_list,
    const PrimaryPlane* primary_plane,
    std::vector<OverlayProposedCandidate>* candidates,
    std::vector<gfx::Rect>* content_bounds) {
  auto* render_pass = render_pass_list->back().get();
  QuadList& quad_list = render_pass->quad_list;
  OverlayCandidate candidate;
  auto overlay_iter = quad_list.end();
  OverlayCandidateFactory candidate_factory = OverlayCandidateFactory(
      render_pass, resource_provider, surface_damage_rect_list,
      &output_color_matrix, GetPrimaryPlaneDisplayRect(primary_plane),
      &render_pass_filters);

  // Original code did reverse iteration.
  // Here we do forward but find the last one, which should be the same thing.
  for (auto it = quad_list.begin(); it != quad_list.end(); ++it) {
    if (OverlayCandidate::IsInvisibleQuad(*it)) {
      continue;
    }

    // Look for quads that are overlayable and require an overlay. Chromecast
    // only supports a video underlay so this can't promote all quads that are
    // overlayable, it needs to ensure that the quad requires overlays since
    // that quad is side-channeled through a secure path into an overlay
    // sitting underneath the primary plane. This is only looking at where the
    // quad is supposed to be to replace it with a transparent quad to allow
    // the underlay to be visible.
    // VIDEO_HOLE implies it requires overlay.
    if (it->material == DrawQuad::Material::kVideoHole &&
        candidate_factory.FromDrawQuad(*it, candidate) ==
            OverlayCandidate::CandidateStatus::kSuccess) {
      overlay_iter = it;
    }
  }

  if (overlay_iter != quad_list.end()) {
    // [ABHIJEET][PUNCH-OUT] Log overlay candidate found
    std::string process_name = "unknown";
    auto* cmd = base::CommandLine::ForCurrentProcess();
    if (cmd->HasSwitch(switches::kProcessType)) {
      process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
    }
    base::ProcessId pid = base::GetCurrentProcId();
    
    const VideoHoleDrawQuad* video_hole = VideoHoleDrawQuad::MaterialCast(*overlay_iter);
    LOG(INFO) << "[ABHIJEET][PUNCH-OUT] OverlayStrategyUnderlayStarboard::Propose - VIDEO HOLE FOUND"
              << " | Process: " << process_name << " | PID: " << pid
              << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
              << " | Thread Name: " << base::PlatformThread::GetName()
              << " | Overlay Plane ID: " << video_hole->overlay_plane_id.ToString()
              << " | Display Rect: " << candidate.display_rect.ToString()
              << " | PURPOSE: Compositor found video hole quad for overlay processing";
    
    candidates->emplace_back(overlay_iter, candidate, this);
    
    // [ABHIJEET][PUNCH-OUT] SIMPLE DEBUG: Log detailed hole information for manual verification
    LOG(INFO) << "[ABHIJEET][PUNCH-OUT] HOLE DETAILS FOR MANUAL VERIFICATION"
              << " | Hole Rect: " << video_hole->rect.ToString()
              << " | Display Rect: " << candidate.display_rect.ToString()  
              << " | Overlay Plane ID: " << video_hole->overlay_plane_id.ToString()
              << " | MANUAL CHECK: Look for transparent rectangular area at these coordinates";
              
#if 0  // DISABLED: Compositor debug quads not working - trying different approach
    // [ABHIJEET][PUNCH-OUT] DEBUG: Visual debugging borders around video holes
    // SAFE VISUAL DEBUGGING APPROACH:
    // Add colored borders AROUND video holes to make them easily visible.
    // This approach is safe because:
    // 1. Border goes AROUND the hole, not blocking the transparent area
    // 2. Doesn't interfere with punch-out mechanism
    // 3. SbPlayer can still render through the transparent hole
    // 4. Easy to toggle on/off with #if 0
    
    LOG(INFO) << "[ABHIJEET][PUNCH-OUT] ADDING DEBUG BORDER around video hole"
              << " | Hole Rect: " << video_hole->rect.ToString() 
              << " | Border Color: RED (easy visual identification)"
              << " | Border Width: 4px (clearly visible)"
              << " | Method: 4 SolidColorDrawQuads (more reliable than DebugBorderDrawQuad)"
              << " | PURPOSE: Visual debugging - see exactly where holes are created";
    
    // SIMPLER APPROACH: Create ONE large solid color quad that overlaps the hole
    // This is more likely to work and be visible
    gfx::Rect hole_rect = video_hole->rect;
    int border_width = 8;  // Thicker border for visibility
    
    // Create a large red rectangle that encompasses the hole area plus border
    // The hole will be rendered on top, creating a border effect
    gfx::Rect border_rect(hole_rect.x() - border_width,
                          hole_rect.y() - border_width, 
                          hole_rect.width() + 2*border_width,
                          hole_rect.height() + 2*border_width);
                          
    // Ensure we don't go into negative coordinates (which might get clipped)
    if (border_rect.x() < 0) {
      border_rect.set_width(border_rect.width() + border_rect.x());
      border_rect.set_x(0);
    }
    if (border_rect.y() < 0) {
      border_rect.set_height(border_rect.height() + border_rect.y());
      border_rect.set_y(0);
    }
    
    LOG(INFO) << "[ABHIJEET][PUNCH-OUT] Creating LARGE RED quad behind hole"
              << " | Hole Rect: " << hole_rect.ToString()
              << " | Border Rect: " << border_rect.ToString()
              << " | Strategy: Large red quad behind hole creates border effect";
    
    // Create ONE large red quad that will appear behind/around the hole
    auto* border_quad = render_pass->CreateAndAppendDrawQuad<SolidColorDrawQuad>();
    border_quad->SetNew(video_hole->shared_quad_state,
                        border_rect,     // Large rectangle including border area
                        border_rect,     // Same for visible rect  
                        SkColor4f{1.0f, 0.0f, 0.0f, 1.0f},  // RED color (RGBA floats)
                        false);          // not anti-aliased
    
    LOG(INFO) << "[ABHIJEET][PUNCH-OUT] DEBUG BORDER ADDED successfully"
              << " | Method: ONE large RED SolidColorDrawQuad behind hole"  
              << " | Border Quad: " << border_rect.ToString()
              << " | Effect: RED background visible around transparent hole"
              << " | Video hole remains transparent for SbPlayer punch-out"
              << " | RED area shows exact hole boundaries for debugging";
              
#endif // End debug border code - change to #if 0 to disable
  }
}

bool OverlayStrategyUnderlayStarboard::Attempt(
    const SkM44& output_color_matrix,
    const OverlayProcessorInterface::FilterOperationsMap& render_pass_filters,
    const OverlayProcessorInterface::FilterOperationsMap&
        render_pass_backdrop_filters,
    DisplayResourceProvider* resource_provider,
    AggregatedRenderPassList* render_pass_list,
    SurfaceDamageRectList* surface_damage_rect_list,
    const PrimaryPlane* primary_plane,
    OverlayCandidateList* candidate_list,
    std::vector<gfx::Rect>* content_bounds,
    const OverlayProposedCandidate& proposed_candidate) {
  // Before we attempt an overlay strategy, the candidate list should be empty.
  DCHECK(candidate_list->empty());
  auto* render_pass = render_pass_list->back().get();
  QuadList& quad_list = render_pass->quad_list;
  bool found_underlay = false;
  gfx::Rect content_rect;
  OverlayCandidateFactory candidate_factory = OverlayCandidateFactory(
      render_pass, resource_provider, surface_damage_rect_list,
      &output_color_matrix, GetPrimaryPlaneDisplayRect(primary_plane),
      &render_pass_filters);

  for (const auto* quad : base::Reversed(quad_list)) {
    if (OverlayCandidate::IsInvisibleQuad(quad)) {
      continue;
    }

    const auto& transform = quad->shared_quad_state->quad_to_target_transform;
    gfx::Rect quad_rect = transform.MapRect(quad->rect);

    bool is_underlay = false;
    if (!found_underlay) {
      OverlayCandidate candidate;
      // Look for quads that are overlayable and require an overlay. Chromecast
      // only supports a video underlay so this can't promote all quads that are
      // overlayable, it needs to ensure that the quad requires overlays since
      // that quad is side-channeled through a secure path into an overlay
      // sitting underneath the primary plane. This is only looking at where the
      // quad is supposed to be to replace it with a transparent quad to allow
      // the underlay to be visible.
      // VIDEO_HOLE implies it requires overlay.
      is_underlay = quad->material == DrawQuad::Material::kVideoHole &&
                    candidate_factory.FromDrawQuad(quad, candidate) ==
                        OverlayCandidate::CandidateStatus::kSuccess;
      found_underlay = is_underlay;
    }

    if (!found_underlay && quad->material == DrawQuad::Material::kSolidColor) {
      const SolidColorDrawQuad* solid = SolidColorDrawQuad::MaterialCast(quad);
      if (solid->color == SkColors::kBlack) {
        continue;
      }
    }

    if (is_underlay) {
      content_rect.Subtract(quad_rect);
    } else {
      content_rect.Union(quad_rect);
    }
  }

  if (is_using_overlay_ != found_underlay) {
    is_using_overlay_ = found_underlay;
    LOG(INFO) << (found_underlay ? "Overlay activated" : "Overlay deactivated");
  }

  if (found_underlay) {
    for (auto it = quad_list.begin(); it != quad_list.end(); ++it) {
      OverlayCandidate candidate;
      if (it->material != DrawQuad::Material::kVideoHole ||
          candidate_factory.FromDrawQuad(*it, candidate) !=
              OverlayCandidate::CandidateStatus::kSuccess) {
        continue;
      }

      OverlayProposedCandidate proposed_to_commit(it, candidate, this);
      CommitCandidate(proposed_to_commit, render_pass);
    }
  }

  DCHECK(content_bounds && content_bounds->empty());
  if (found_underlay) {
    content_bounds->push_back(content_rect);
  }
  return found_underlay;
}

void OverlayStrategyUnderlayStarboard::CommitCandidate(
    const OverlayProposedCandidate& proposed_candidate,
    AggregatedRenderPass* render_pass) {
  // [ABHIJEET][PUNCH-OUT] Log CommitCandidate - KEY COMPOSITOR OPERATION
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  const VideoHoleDrawQuad* video_hole = VideoHoleDrawQuad::MaterialCast(*proposed_candidate.quad_iter);
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] OverlayStrategyUnderlayStarboard::CommitCandidate - COMMITTING OVERLAY"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName()
            << " | Overlay Plane ID: " << video_hole->overlay_plane_id.ToString()
            << " | Display Rect: " << proposed_candidate.candidate.display_rect.ToString()
            << " | Has Mask Filter: " << (proposed_candidate.candidate.has_mask_filter ? "YES" : "NO")
            << " | PURPOSE: Compositor committing video overlay and replacing quad with transparent hole";
  
  DCHECK(GetVideoGeometrySetter());
  
  // ============================================================================
  // STEP 1 OF 4: COMPOSITOR(GPU) → BROWSER PROCESS GEOMETRY FORWARDING
  // ============================================================================
  // 
  // VALIDATED 4-STEP GEOMETRY FLOW:
  // 1. [THIS STEP] Compositor(GPU) → Browser Process (VideoGeometrySetterService)
  // 2. [NEXT] Browser Process → StarboardRendererClient(Renderer Process)  
  // 3. [NEXT] StarboardRendererClient(Renderer) → StarboardRenderer(GPU Process)
  // 4. [FINAL] StarboardRenderer(GPU) → SbPlayer Platform (actual video positioning)
  //
  // This is the FIRST step where Compositor thread in GPU Process sends geometry
  // updates to Browser Process VideoGeometrySetterService for N:M coordination.
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] OverlayStrategyUnderlayStarboard::CommitCandidate - CALLING SetVideoGeometry"
            << " | VideoGeometrySetter Available: " << (GetVideoGeometrySetter() ? "YES" : "NO")
            << " | STEP: 1/4 - Compositor(GPU) → Browser Process"
            << " | PURPOSE: Starting 4-step geometry flow to position video overlay";
  
  GetVideoGeometrySetter()->SetVideoGeometry(
      proposed_candidate.candidate.display_rect,
      absl::get<gfx::OverlayTransform>(proposed_candidate.candidate.transform),
      VideoHoleDrawQuad::MaterialCast(*proposed_candidate.quad_iter)
          ->overlay_plane_id);
  
  // ============================================================================
  // STEP 4/4: HOLE REPLACEMENT - FINAL COMPOSITOR PROCESSING
  // ============================================================================
  process_name = "unknown";
  cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  pid = base::GetCurrentProcId();
  
  if (proposed_candidate.candidate.has_mask_filter) {
    LOG(INFO) << "[ABHIJEET][PUNCH-OUT] OverlayStrategyUnderlayStarboard::CommitCandidate - STEP 4/4: HOLE REPLACEMENT (MASK)"
              << " | Process: " << process_name << " | PID: " << pid
              << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
              << " | Thread Name: " << base::PlatformThread::GetName()
              << " | Blend Mode: kDstOut"
              << " | STEP: 4/4 - Replace hole with BLACK quad (GPU Process - Compositor Thread)"
              << " | PURPOSE: Black quad with destination out blending for masked overlay";
    render_pass->ReplaceExistingQuadWithSolidColor(
        proposed_candidate.quad_iter, SkColors::kBlack, SkBlendMode::kDstOut);
  } else {
    LOG(INFO) << "[ABHIJEET][PUNCH-OUT] OverlayStrategyUnderlayStarboard::CommitCandidate - STEP 4/4: HOLE REPLACEMENT (TRANSPARENT)"
              << " | Process: " << process_name << " | PID: " << pid
              << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
              << " | Thread Name: " << base::PlatformThread::GetName()
              << " | Blend Mode: kSrcOver"
              << " | STEP: 4/4 - Replace hole with TRANSPARENT quad (GPU Process - Compositor Thread)"
              << " | PURPOSE: Transparent quad allows video underlay to show through";
    render_pass->ReplaceExistingQuadWithSolidColor(proposed_candidate.quad_iter,
                                                   SkColors::kTransparent,
                                                   SkBlendMode::kSrcOver);
  }
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] OverlayStrategyUnderlayStarboard::CommitCandidate - STEP 4/4 COMPLETE: HOLE PROCESSING FINISHED"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread: " << base::PlatformThread::GetName()
            << " | FINAL RESULT: Video hole replaced with transparent/black quad for punch-out display";
}

// Turn on blending for the output surface plane so the underlay could show
// through.
void OverlayStrategyUnderlayStarboard::AdjustOutputSurfaceOverlay(
    OverlayProcessorInterface::OutputSurfaceOverlayPlane*
        output_surface_plane) {
  if (output_surface_plane) {
    output_surface_plane->enable_blending = true;
  }
}

OverlayStrategy OverlayStrategyUnderlayStarboard::GetUMAEnum() const {
  return OverlayStrategy::kUnderlay;
}

// static
void OverlayStrategyUnderlayStarboard::ConnectVideoGeometrySetter(
    mojo::PendingRemote<cobalt::media::mojom::VideoGeometrySetter>
        video_geometry_setter) {
  // [ABHIJEET][PUNCH-OUT] Log VideoGeometrySetter connection - CRITICAL SETUP
  std::string process_name = "unknown";
  auto* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kProcessType)) {
    process_name = cmd->GetSwitchValueASCII(switches::kProcessType);
  }
  base::ProcessId pid = base::GetCurrentProcId();
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] OverlayStrategyUnderlayStarboard::ConnectVideoGeometrySetter"
            << " | Process: " << process_name << " | PID: " << pid
            << " | Thread ID: [" << base::PlatformThread::CurrentId() << "]"
            << " | Thread Name: " << base::PlatformThread::GetName()
            << " | PURPOSE: Connecting Compositor to Browser Process VideoGeometrySetterService";
  
  GetVideoGeometrySetter().Bind(std::move(video_geometry_setter));
  
  LOG(INFO) << "[ABHIJEET][PUNCH-OUT] OverlayStrategyUnderlayStarboard::ConnectVideoGeometrySetter - CONNECTED"
            << " | VideoGeometrySetter Bound: " << (GetVideoGeometrySetter().is_bound() ? "YES" : "NO")
            << " | PURPOSE: Compositor can now send geometry updates to Browser Process hub";
}

}  // namespace viz
