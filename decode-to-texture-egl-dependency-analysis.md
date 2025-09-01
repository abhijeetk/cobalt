# Decode-to-Texture EGL Dependency Analysis

## Investigation Summary

This document analyzes whether the stable implementation of decode-to-texture in Cobalt depends on EGL, examining both Android and tvOS implementations based on the `origin/23.lts.stable` branch and available tvOS branches.

## Key Findings

### DecodeTargetProvider (Chromium Media Layer) - NO Direct EGL Dependency ✅

The stable `DecodeTargetProvider` class (`cobalt/media/base/decode_target_provider.h`) **does NOT directly depend on EGL**:

```cpp
// Only includes base Chromium utilities - NO EGL headers
#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "base/time/time.h"
#include "starboard/decode_target.h"  // Starboard abstraction layer
```

### DecodeTargetProvider Functionality
- **Pure abstraction layer**: Acts as bridge between Chromium media pipeline and Starboard
- **No graphics code**: Only manages callbacks and state
- **Platform agnostic**: Works with any Starboard implementation

## Platform-Specific Analysis

### 1. Android Implementation - EGL Dependent ⚠️

**Source**: `origin/23.lts.stable` branch
**File**: `starboard/android/shared/decode_target.cc`

```cpp
#include <EGL/egl.h>              // ✅ Direct EGL dependency
#include <GLES2/gl2.h>            // ✅ OpenGL ES dependency  
#include <GLES2/gl2ext.h>
```

#### SbDecodeTargetGraphicsContextProvider Structure
```cpp
typedef struct SbDecodeTargetGraphicsContextProvider {
  void* egl_display;    // ✅ Requires EGL display
  void* egl_context;    // ✅ Requires EGL context
  SbDecodeTargetGlesContextRunner gles_context_runner;  // ✅ GLES runner
  void* gles_context_runner_context;
} SbDecodeTargetGraphicsContextProvider;
```

#### Android DecodeTarget Creation
```cpp
// From starboard/android/shared/decode_target.cc:66-69
DecodeTarget::DecodeTarget(SbDecodeTargetGraphicsContextProvider* provider) {
  std::function<void()> closure = 
      std::bind(&DecodeTarget::CreateOnContextRunner, this);
  SbDecodeTargetRunInGlesContext(provider, &RunOnContextRunner, &closure);
}
```

### 2. tvOS Implementation Analysis

#### tvOS Implementation Status in Stable Branch
- **Finding**: `origin/23.lts.stable` branch **does NOT contain tvOS implementation**
- **No tvOS Starboard directory**: `starboard/tvos/` does not exist in stable branch
- **Conclusion**: tvOS decode-to-texture implementation not available in stable release

#### tvOS Implementation in Development Branches
**Source**: `remotes/igalia/24.tvos.41+` branch (development)
**File**: `internal/starboard/shared/uikit/video_decoder.mm`

```objc
#include <GLES2/gl2.h>              // ✅ OpenGL ES dependency
#include <GLES2/gl2ext.h>
#include "internal/starboard/shared/uikit/egl_adapter.h"  // ✅ EGL adapter
```

#### tvOS EGL Architecture
**Source**: `remotes/igalia/24.tvos.41+:internal/starboard/shared/uikit/video_decoder.mm`
```objc
// tvOS uses EGL abstraction over iOS OpenGL ES (EAGL), not Metal
@autoreleasepool {
  SBDEglAdapter* egl_adapter = SBDGetApplication().eglAdapter;
  EAGLContext* egl_context = [egl_adapter applicationContextForStarboardContext:
      decode_target_graphics_context_provider_->egl_context];
  // EAGLContext = iOS OpenGL ES context (not Metal)
  CVEAGLContext context = egl_context;
  // Creates OpenGL ES texture cache for video frames
  CVReturn status = CVOpenGLESTextureCacheCreate(
      kCFAllocatorDefault, nullptr, context, nullptr, &texture_cache_);
}
```

#### tvOS EGL Adapter Interface
**Source**: `remotes/igalia/24.tvos.41+:internal/starboard/shared/uikit/egl_adapter.h`
```objc
#import <Foundation/Foundation.h>
#include <EGL/egl.h>                // ✅ Direct EGL dependency
#import "internal/starboard/shared/uikit/egl_surface.h"

// Key methods from SBDEglAdapter class:
- (EAGLContext*)contextWithSharedContext:(nullable EAGLContext*)sharedContext;
- (EAGLContext*)applicationContextForStarboardContext:(EGLContext)context;
// EAGLContext = iOS OpenGL ES context, not Metal context
```

### 3. Starboard EGL Abstraction Layer

**Source**: `origin/23.lts.stable:starboard/egl.h`
- **Purpose**: Platform-agnostic EGL interface abstraction
- **Implementation**: Uses `SbGetEglInterface()` for platform-specific EGL functions
- **Abstraction Level**: Wraps native EGL calls with Starboard types

```cpp
// Starboard EGL abstraction (from starboard/egl.h)
#define EGL_CALL_PREFIX SbGetEglInterface()->
#define EGLConfig SbEglConfig
#define EGLint SbEglInt32
#define EGLNativeWindowType SbEglNativeWindowType
```

#### FakeGraphicsContextProvider EGL Usage
**File**: `starboard/testing/fake_graphics_context_provider.cc`
```cpp
// Uses Starboard EGL abstraction, not direct EGL
#define EGL_CALL_PREFIX SbGetEglInterface()->
// Creates EGL context through Starboard interface
```

## Architecture Analysis

### Three-Layer Architecture
```
┌─────────────────────────────────────────────────────────────┐
│                    Chromium Media Layer                     │
│  DecodeTargetProvider (cobalt/media/base/)                 │
│  ✅ NO EGL dependency - pure abstraction                    │
│  - Manages OutputMode (punch-out vs decode-to-texture)     │
│  - Provides callback bridge to Starboard                   │
│  - Platform agnostic                                       │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                  Starboard Abstraction Layer               │
│  starboard/egl.h, starboard/decode_target.h               │
│  ✅ EGL abstracted through SbGetEglInterface()              │
│  - Platform-agnostic EGL interface                         │
│  - SbDecodeTargetGraphicsContextProvider structure         │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                Platform-Specific Implementation            │
│  Android: Direct EGL    │    tvOS: EGL over Metal/UIKit   │
│  ⚠️  EGL/GLES required   │    ⚠️  EGL adapter required      │
│  - Native EGL calls     │    - EGL-to-Metal bridge        │
│  - GL texture creation  │    - EAGL context management     │
└─────────────────────────────────────────────────────────────┘
```

## Platform Comparison Summary

| Platform | Stable Branch Support | EGL Dependency | Graphics API | Implementation Status |
|----------|----------------------|----------------|--------------|---------------------|
| **Android** | ✅ Available | ✅ YES (Direct) | OpenGL ES | Complete in `origin/23.lts.stable` |
| **tvOS** | ❌ Not Available | ✅ YES (Abstracted) | EAGL+EGL Adapter | Development branches only |
| **Linux** | ✅ Available | ✅ YES (Direct) | OpenGL ES | Complete in `origin/23.lts.stable` |
| **Testing** | ✅ FakeProvider | ✅ YES (Abstracted) | Starboard EGL | Complete in all branches |

## EGL Dependency Summary

| Component | Android | tvOS | Reason |
|-----------|---------|------|---------|
| **DecodeTargetProvider** (Stable) | ❌ NO | ❌ NO | Pure abstraction - no direct graphics code |
| **SbDecodeTarget interface** | ❌ NO | ❌ NO | Abstract C API |
| **Starboard EGL Layer** | 🔄 ABSTRACTED | 🔄 ABSTRACTED | Uses `SbGetEglInterface()` |
| **Platform Implementation** | ✅ YES (Direct) | ✅ YES (Adapter) | Creates GL textures, needs EGL context |

## Key Insights

### 1. Stable Implementation is EGL-Agnostic at Media Layer ✅
The stable `DecodeTargetProvider` from `origin/23.lts.stable` is **platform-agnostic** and doesn't directly depend on EGL. It's a pure abstraction layer that works regardless of underlying graphics API.

### 2. EGL Dependency is Platform-Implementation Specific ⚠️
EGL dependency exists at the **platform implementation level**, not in the stable DecodeTargetProvider interface:
- **Android**: Direct EGL usage for GL texture creation
- **tvOS**: EGL abstraction layer over Metal/UIKit graphics

### 3. tvOS Uses EGL-to-EAGL Bridge 🔄
**CORRECTION**: tvOS implementation doesn't use Metal but uses iOS OpenGL ES (EAGL):
- **EGL Adapter**: Translates EGL calls to EAGL (iOS OpenGL ES) operations
- **EAGLContext**: iOS OpenGL ES context management (not Metal context)
- **Texture Cache**: CVOpenGLESTextureCacheCreate for OpenGL ES video frame textures
- **Source**: Analysis of `remotes/igalia/24.tvos.41+:internal/starboard/shared/uikit/` files

### 4. Starboard Abstraction Enables Graphics API Flexibility 🎯
The Starboard layer provides EGL abstraction that enables different underlying implementations:
- **Android**: Native EGL/OpenGL ES
- **tvOS**: EGL-to-EAGL (iOS OpenGL ES) adapter
- **Future platforms**: Could use Vulkan, Direct3D, Metal, etc.

## Implications for Integration

### For Current Android Work
1. **Use stable DecodeTargetProvider**: No EGL concerns at media layer
2. **Android implementation requires EGL**: Current Starboard Android uses direct EGL
3. **Platform abstraction maintained**: Media logic separated from graphics implementation

### For tvOS Development
1. **Stable DecodeTargetProvider compatible**: Would work with tvOS EGL adapter
2. **tvOS not in stable branch**: Need development branch for tvOS implementation
3. **EGL adapter approach**: tvOS bridges EGL calls to EAGL (iOS OpenGL ES), not Metal

### For Cross-Platform Support
The stable implementation provides excellent separation:
- ✅ **Media logic**: Platform-independent DecodeTargetProvider
- 🔄 **Graphics abstraction**: Starboard EGL interface 
- ⚠️ **Platform specifics**: Each platform implements EGL interface differently
  - Android: Direct EGL/OpenGL ES
  - tvOS: EGL-to-EAGL adapter

## Conclusion

**The stable decode-to-texture implementation has layered EGL dependency**:

1. ✅ **DecodeTargetProvider (stable)**: NO direct EGL dependency - pure abstraction
2. 🔄 **Starboard layer**: EGL abstracted through `SbGetEglInterface()`
3. ⚠️ **Platform implementations**: 
   - **Android**: Direct EGL dependency for GL texture creation
   - **tvOS**: EGL adapter over EAGL/iOS OpenGL ES (development branches only)

**Key Finding**: The stable branch contains complete decode-to-texture support for Android but **lacks tvOS implementation**. tvOS decode-to-texture is available in development branches (`remotes/igalia/24.tvos.41+`) and uses an **EGL-to-EAGL adapter approach** (iOS OpenGL ES, not Metal).

The stable implementation provides excellent abstraction that would support tvOS once the platform-specific EGL-to-EAGL adapter is integrated from development branches.

---
*Analysis conducted: 2025-08-25 - Investigation of EGL dependencies in stable decode-to-texture implementation for Android and tvOS*