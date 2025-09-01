# Punch-Out Video Visual Debugging Attempts

This document tracks all the visual debugging approaches we tried to identify punch-out video holes for easier development and troubleshooting.

## Problem Statement
We need visual identification of punch-out video holes during development to:
- See exactly where transparent holes are created
- Debug video positioning issues  
- Verify hole geometry matches video content
- Troubleshoot punch-out rendering pipeline

## Visual Debugging Attempts

### 1. Red VideoFrame Approach (FAILED - Blocked Video)

**File**: `/media/abhijeet/Chromium-Ubuntu/cobalt/chromium/src/media/renderers/video_overlay_factory.cc:108`

**Strategy**: Replace `VideoFrame::CreateVideoHoleFrame()` with `VideoFrame::CreateColorFrame()` to create red holes instead of transparent ones.

**Implementation**:
```cpp
// FAILED APPROACH - Red holes blocked video playback
scoped_refptr<VideoFrame> frame =
    VideoFrame::CreateColorFrame(size,           // natural size  
                                SK_ColorRED,     // Red color for debugging
                                base::TimeDelta()); // timestamp
```

**Result**: ❌ **FAILED**
- Red holes remained visible even during video playback
- Audio played but video was blocked by red color
- Problem: `CreateColorFrame()` renders ON TOP instead of creating transparent holes
- **User feedback**: "Red color hole is red even after I start playing a video, I thought SBPlayer will play on top of it but I listen audio but video is red"

**Reverted**: Back to `VideoFrame::CreateVideoHoleFrame()` for proper transparent holes.

### 2. Green SbPlayer Background (FAILED - Blocked Video)

**File**: `/media/abhijeet/Chromium-Ubuntu/cobalt/chromium/src/cobalt/android/apk/app/src/main/java/dev/cobalt/media/VideoSurfaceView.java`

**Strategy**: Set SbPlayer hardware surface background to green for visual identification.

**Implementation**:
```java
// FAILED APPROACH - Green background blocked video playback
setBackgroundColor(Color.GREEN);  // Green debugging background
```

**Result**: ❌ **FAILED** 
- Video playback completely blocked
- Green background prevented video from rendering
- **User feedback**: "But after this green color changes, I am not able to see the playback"

**Reverted**: Back to `Color.TRANSPARENT` for proper video rendering.

### 3. Compositor Debug Borders (FAILED - Not Visible)

**File**: `/media/abhijeet/Chromium-Ubuntu/cobalt/chromium/src/components/viz/service/display/starboard/overlay_strategy_underlay_starboard.cc`

**Strategy**: Add debug borders in compositor using various quad approaches.

#### 3a. DebugBorderDrawQuad Approach
```cpp
auto* debug_border = render_pass->CreateAndAppendDrawQuad<DebugBorderDrawQuad>();
debug_border->SetNew(video_hole->shared_quad_state,
                     border_rect, border_rect,
                     SkColor4f{1.0f, 0.0f, 0.0f, 1.0f}, // Red
                     4); // 4px width
```

#### 3b. Multiple SolidColorDrawQuad Borders
```cpp
// Create 4 rectangles around hole for border effect
// Top, bottom, left, right border quads
```

#### 3c. Single Large SolidColorDrawQuad
```cpp
auto* border_quad = render_pass->CreateAndAppendDrawQuad<SolidColorDrawQuad>();
border_quad->SetNew(video_hole->shared_quad_state,
                    border_rect,     // Large rectangle including border area
                    border_rect,     // Same for visible rect  
                    SkColor4f{1.0f, 0.0f, 0.0f, 1.0f},  // RED color
                    false);          // not anti-aliased
```

**Result**: ❌ **FAILED**
- Logs confirmed code execution: `[ABHIJEET][PUNCH-OUT] ADDING DEBUG BORDER around video hole`
- But no visual borders appeared on screen
- **User feedback**: "But I don't see the red border" and "Still no red color identification"
- Possible issues: Compositor debug quads not rendered in this pipeline or processed differently

**Status**: Currently disabled with `#if 0`

### 4. VideoHoleDrawQuad Blending Modification (REVERTED)

**File**: `/media/abhijeet/Chromium-Ubuntu/cobalt/chromium/src/components/viz/common/quads/video_hole_draw_quad.cc`

**Strategy**: Enable blending on VideoHoleDrawQuad to make it visible instead of transparent.

**Implementation**:
```cpp
DrawQuad::SetAll(shared_quad_state, DrawQuad::Material::kVideoHole, rect,
                 visible_rect,
                 /*needs_blending=*/true);  // Enable blending for visibility
```

**Result**: ❌ **REVERTED**
- VideoHoleDrawQuad is designed to be invisible by nature
- Enabling blending doesn't make it visible - it's still a hole
- Approach was fundamentally flawed

### 5. Early Pipeline Debug Borders (CURRENT - NOT WORKING)

**File**: `/media/abhijeet/Chromium-Ubuntu/cobalt/chromium/src/media/renderers/video_resource_updater.cc:737-773`

**Strategy**: Add debug borders at VideoHoleDrawQuad creation time, before compositor processing.

**Implementation**:
```cpp
#if 1  // [ABHIJEET][PUNCH-OUT] VISUAL DEBUG: Add red outline quad around video hole
// Create red background quad BEFORE hole quad (so hole renders on top)
auto* debug_quad = render_pass->CreateAndAppendDrawQuad<viz::SolidColorDrawQuad>();
debug_quad->SetNew(shared_quad_state, border_rect, border_rect,
                  SkColor4f{1.0f, 0.0f, 0.0f, 0.8f},  // Semi-transparent red
                  false);  // not anti-aliased
#endif
```

**Theory**: Create red quad BEFORE VideoHoleDrawQuad so hole renders transparently on top, creating border effect.

**Result**: ❌ **NOT WORKING** (as reported by user)
- Added missing `solid_color_draw_quad.h` include
- Code compiles and should execute
- But still no visible debugging according to user feedback

## Technical Analysis

### Why Visual Debugging Is Difficult

1. **Punch-Out Nature**: Holes are designed to be transparent by definition
2. **Hardware Overlay**: Video renders through hardware overlay underneath compositor
3. **Rendering Order**: Complex interaction between compositor quads and hardware surfaces
4. **Pipeline Complexity**: Multiple processes (Renderer→Browser→GPU) with different rendering contexts

### Successful Logging Approach

While visual debugging failed, comprehensive logging has been successful:
- Process and thread identification
- Overlay plane ID tracking  
- Geometry coordinates
- 4-step hole processing flow
- Cross-process communication traces

**Example successful log**:
```
[ABHIJEET][PUNCH-OUT] HOLE DETAILS FOR MANUAL VERIFICATION | Hole Rect: 0,0 854x480 | Display Rect: 0,0 854x480 | Overlay Plane ID: 12345 | MANUAL CHECK: Look for transparent rectangular area at these coordinates
```

## Recommendations

### Alternative Debugging Approaches to Try

1. **Android Developer Tools**:
   - Use Android Studio Layout Inspector
   - Enable GPU rendering profiling
   - Use hardware compositor debugging tools

2. **Platform-Level Debugging**:
   - Android SurfaceFlinger debugging
   - Hardware overlay debugging via adb
   - Video surface inspection tools

3. **Custom UI Overlay**:
   - Add Android UI elements (Views) on top of video area
   - Use Canvas drawing for debug borders at Android UI level
   - Bypass compositor entirely

4. **Video Content Modification**:
   - Use test videos with colored borders
   - Modify video content to have built-in positioning guides
   - Use videos with timestamp/coordinate overlays

### Current Status

- All compositor-level visual debugging attempts have failed
- Logging-based debugging is working successfully  
- Manual coordinate verification is the current approach
- Need alternative debugging methods outside compositor pipeline

### Files Modified (All Debug Code Currently Disabled)

1. `video_overlay_factory.cc` - Reverted red VideoFrame approach
2. `VideoSurfaceView.java` - Reverted green background approach  
3. `overlay_strategy_underlay_starboard.cc` - Disabled debug borders with `#if 0`
4. `video_hole_draw_quad.cc` - Reverted blending modification
5. `video_resource_updater.cc` - Early pipeline debug borders (not working)

All visual debugging code can be easily re-enabled by changing `#if 0` to `#if 1` for future experiments.