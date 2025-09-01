# Decode-to-Texture Implementation Branches Analysis

## Investigation Summary

This document analyzes the availability of DecodeTargetProvider/VideoFrameProvider implementation across different branches in the Cobalt codebase to understand where decode-to-texture functionality can be found.

## Key Findings

### 1. Primary Implementation Branches

#### `origin/media_tools` Branch ✅
- **Status**: Complete DecodeTargetProvider implementation available
- **File**: `cobalt/media/base/decode_target_provider.h`
- **Features**:
  - Full DecodeTargetProvider class with OutputMode enum
  - Methods: `SetOutputMode()`, `GetOutputMode()`, `SetGetCurrentSbDecodeTargetFunction()`
  - Support for both punch-out and decode-to-texture modes
  - Bridge functionality to Starboard SbDecodeTarget APIs

#### `origin/23.lts.stable` Branch ✅
- **Status**: Complete DecodeTargetProvider implementation available
- **File**: `cobalt/media/base/decode_target_provider.h`
- **Features**: Similar to media_tools branch
- **Note**: This is an LTS branch, indicating stable decode-to-texture support

### 2. Legacy Implementation

#### `origin/22.lts.stable` Branch ✅
- **Status**: Has VideoFrameProvider (predecessor to DecodeTargetProvider)
- **File**: `cobalt/media/base/video_frame_provider.h`
- **Note**: Original name before the rename to DecodeTargetProvider

### 3. Current Main Branch ❌
- **Status**: DecodeTargetProvider header missing
- **Issue**: Feature flag `COBALT_MEDIA_ENABLE_DECODE_TARGET_PROVIDER=0` disabled
- **Reason**: Incomplete integration after merge

## Commit History Analysis

### DecodeTargetProvider Evolution
- **Rename Commit**: `71f504bd3de2` - "Rename VideoFrameProvider to DecodeTargetProvider"
- **Available in branches**:
  - `origin/23.lts.stable`
  - `origin/23.lts.1+`
  - `abhijeetk/23.lts.stable`
  - `abhijeetk/23.lts.1+`

### Original Implementation
- **VideoFrameProvider** was the original implementation name
- Found in `origin/22.lts.stable` and earlier versions
- Provided same functionality with different class name

## Implementation Comparison

### Complete Implementation (media_tools/23.lts.stable)
```cpp
class DecodeTargetProvider : public base::RefCountedThreadSafe<DecodeTargetProvider> {
 public:
  enum OutputMode {
    kOutputModePunchOut,
    kOutputModeDecodeToTexture,
    kOutputModeInvalid,
  };

  // Core functionality
  void SetOutputMode(OutputMode output_mode);
  OutputMode GetOutputMode() const;
  
  // Starboard bridge
  void SetGetCurrentSbDecodeTargetFunction(GetCurrentSbDecodeTargetFunction function);
  void ResetGetCurrentSbDecodeTargetFunction();
  SbDecodeTarget GetCurrentSbDecodeTarget() const;
};
```

### Current Main Branch (Incomplete)
- Header file missing: `cobalt/media/base/decode_target_provider.h`
- Build flag disabled: `COBALT_MEDIA_ENABLE_DECODE_TARGET_PROVIDER=0`
- SbPlayerBridge expects DecodeTargetProvider but cannot find implementation

## Current FakeGraphicsContextProvider Analysis

### What We Have ✅
- **File**: `starboard/testing/fake_graphics_context_provider.h`
- **Purpose**: Testing infrastructure for DTT
- **Features**:
  - Complete EGL/OpenGL context setup
  - Thread management for graphics operations
  - Decode target lifecycle management
  - Used in 18+ test files - proves DTT can work

### What We're Missing ❌
- **Interface mismatch**: FakeGraphicsContextProvider doesn't implement DecodeTargetProvider interface
- **Missing methods**: `SetOutputMode()`, `kOutputModeDecodeToTexture`, etc.
- **Integration gap**: No bridge between fake implementation and expected DecodeTargetProvider API

## Recommended Solutions

### Option 1: Use Stable Branch Implementation (Recommended)
```bash
# Copy complete implementation from stable branch
git show origin/23.lts.stable:cobalt/media/base/decode_target_provider.h > cobalt/media/base/decode_target_provider.h

# Enable feature flag
# In media/starboard/BUILD.gn:
"COBALT_MEDIA_ENABLE_DECODE_TARGET_PROVIDER=1"
```

### Option 2: Use Media Tools Branch
```bash
# Copy from media_tools branch (same content as 23.lts.stable)
git show origin/media_tools:cobalt/media/base/decode_target_provider.h > cobalt/media/base/decode_target_provider.h
```

### Option 3: Adapt FakeGraphicsContextProvider (More Complex)
- Create DecodeTargetProvider wrapper around FakeGraphicsContextProvider
- Implement missing interface methods
- Bridge between testing infrastructure and production API

## Branch Availability Summary

| Branch | Implementation | Status | File |
|--------|---------------|--------|------|
| `main` (current) | None | ❌ Missing | N/A |
| `origin/media_tools` | DecodeTargetProvider | ✅ Complete | `cobalt/media/base/decode_target_provider.h` |
| `origin/23.lts.stable` | DecodeTargetProvider | ✅ Complete | `cobalt/media/base/decode_target_provider.h` |
| `origin/22.lts.stable` | VideoFrameProvider | ✅ Legacy | `cobalt/media/base/video_frame_provider.h` |

## Conclusion

**The decode-to-texture implementation is NOT exclusive to `origin/media_tools`**. It's also available in:

1. **`origin/23.lts.stable`** - Stable LTS branch with identical DecodeTargetProvider implementation
2. **`origin/22.lts.stable`** - Legacy VideoFrameProvider implementation (same functionality, different name)

The most reliable source would be **`origin/23.lts.stable`** as it represents a stable, long-term support version with complete decode-to-texture functionality.

## Next Steps

1. **Immediate fix**: Copy DecodeTargetProvider header from `origin/23.lts.stable`
2. **Enable feature flag**: Set `COBALT_MEDIA_ENABLE_DECODE_TARGET_PROVIDER=1`
3. **Test build**: Verify compilation and basic functionality
4. **Validate DTT mode**: Test actual decode-to-texture rendering

---
*Analysis conducted: 2025-08-25 - Investigation of decode-to-texture implementation availability across Cobalt branches*