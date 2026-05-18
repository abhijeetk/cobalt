# Plan: Adding Proper FairPlay Init Data Type Support

**Date:** 2026-05-15
**Status:** Proposal
**Prerequisite:** `FairplayKeySystemInfo` registration (done, committed as `83aabb6a`)

## 1. Current State Comparison

### What WebKit supports (CDMFairPlayStreaming.cpp)

| Init Data Type | Status | Handler |
|---|---|---|
| `"sinf"` | First-class | `sanitizeSinf` / `extractKeyIDsSinf` |
| `"skd"` | First-class | `sanitizeSkd` / `extractKeyIDsSkd` |
| `"cenc"` | Conditional (`HAVE(FAIRPLAYSTREAMING_CENC_INITDATA)`) | Reuses `InitDataRegistry::cencName()` |
| `"mpts"` | Conditional (`HAVE(FAIRPLAYSTREAMING_MTPS_INITDATA)`) | `sanitizeMpts` / `extractKeyIDsMpts` |

### What C25 (old Cobalt) supports

| Init Data Type | Where | Notes |
|---|---|---|
| `"fairplay"` | `application_player.mm:744` | Hardcoded when AVPlayer encounters encrypted content |
| `"cenc"` | `sbplayer_pipeline.cc:241` | Accepted in validation |
| `"keyids"` | `sbplayer_pipeline.cc:241` | Accepted in validation |
| `"webm"` | `sbplayer_pipeline.cc:241` | Accepted in validation |

C25 passes init data type as raw `const char*` string end-to-end. No enum conversion.

### What Chromium currently supports (eme_constants.h)

```cpp
enum class EmeInitDataType { UNKNOWN, WEBM, CENC, KEYIDS };
```

String-to-enum mapping (`encrypted_media_utils.cc:27-37`):
- `"cenc"` -> `CENC`
- `"keyids"` -> `KEYIDS`
- `"webm"` -> `WEBM`
- Everything else -> `UNKNOWN`

### Current workaround in our code

`FairplayKeySystemInfo::IsSupportedInitDataType()` accepts `UNKNOWN`, which lets `"skd"` and
`"sinf"` pass during `requestMediaKeySystemAccess`. But this is fragile:
- `DCHECK(init_data_type != EmeInitDataType::UNKNOWN)` at `web_media_player_impl.cc:1697`
  would crash during actual playback when the `encrypted` event fires with `"fairplay"` type.
- Cannot distinguish between genuinely unknown types and FairPlay types.

## 2. What Needs to Change

### Phase 1: EME Capability Query (requestMediaKeySystemAccess)

This phase is about making the config selector accept FairPlay init data types properly.

**File 1: `media/base/eme_constants.h`**
```cpp
enum class EmeInitDataType {
  UNKNOWN,
  WEBM,
  CENC,
  KEYIDS,
  SINF,      // FairPlay: sinf box init data (key IDs in sinf atoms)
  SKD,       // FairPlay: skd URI init data (skd:// scheme key request)
  FAIRPLAY,  // FairPlay: encrypted media event from AVPlayer
  kMaxValue = FAIRPLAY,
};
```

**File 2: `third_party/blink/renderer/modules/encryptedmedia/encrypted_media_utils.cc`**

Add mappings in `ConvertToInitDataType()`:
```cpp
if (init_data_type == "sinf")
    return media::EmeInitDataType::SINF;
if (init_data_type == "skd")
    return media::EmeInitDataType::SKD;
if (init_data_type == "fairplay")
    return media::EmeInitDataType::FAIRPLAY;
```

Add reverse mappings in `ConvertFromInitDataType()`:
```cpp
case media::EmeInitDataType::SINF:
    return "sinf";
case media::EmeInitDataType::SKD:
    return "skd";
case media::EmeInitDataType::FAIRPLAY:
    return "fairplay";
```

**File 3: `components/cdm/renderer/fairplay_key_system_info.cc`**

Update `IsSupportedInitDataType()` to accept proper types instead of UNKNOWN:
```cpp
bool FairplayKeySystemInfo::IsSupportedInitDataType(
    EmeInitDataType init_data_type) const {
  return init_data_type == EmeInitDataType::SINF ||
         init_data_type == EmeInitDataType::SKD ||
         init_data_type == EmeInitDataType::FAIRPLAY;
}
```

**Files 4-15: Update all `switch` statements on `EmeInitDataType`**

Every `switch` on `EmeInitDataType` needs the new cases to avoid compiler warnings.
These files need updates (add cases that return not-supported or fall through to default):

| File | Action |
|---|---|
| `media/cdm/aes_decryptor.cc` | Add cases returning not-supported |
| `media/cdm/cdm_type_conversion.cc` | Add conversion cases |
| `media/cdm/win/media_foundation_cdm_session.cc` | Add cases (Windows only) |
| `media/cdm/fuchsia/fuchsia_cdm.cc` | Add cases (Fuchsia only) |
| `media/base/android/media_drm_bridge.cc` | Add cases (Android only) |
| `media/starboard/starboard_cdm.cc` | Add FairPlay handling |
| `chromecast/starboard/media/cdm/starboard_decryptor_cast.cc` | Add cases |
| `third_party/blink/renderer/platform/media/web_content_decryption_module_session_impl.cc` | Add string conversions |
| `third_party/blink/renderer/platform/media/key_system_config_selector_unittest.cc` | Add test cases |
| `components/cdm/renderer/external_clear_key_key_system_info.cc` | Add not-supported cases |
| `components/cdm/renderer/android_key_system_info.cc` | Add not-supported cases |
| `chromeos/components/cdm_factory_daemon/mojom/cdm_types_enum_mojom_traits.h` | Add enum mapping |
| `media/cdm/aes_decryptor_fuzztests.cc` | Add to fuzz corpus |

### Phase 2: Playback Path (encrypted event from AVPlayer)

This phase is about the runtime path when AVPlayer encounters encrypted content.

**The flow:**
1. AVPlayer detects encrypted content
2. `application_player.mm:744` fires `_encryptedMediaFunc(... "fairplay" ...)`
3. String flows up through Starboard -> media pipeline -> Blink
4. Blink converts to `EmeInitDataType::FAIRPLAY` (after Phase 1)
5. `DCHECK` at `web_media_player_impl.cc:1697` passes (no longer UNKNOWN)
6. `MediaEncryptedEvent` fires in JS with `initDataType: "fairplay"`
7. JS calls `session.generateRequest("fairplay", initData)`
8. Flows back down to `SbDrmGenerateSessionUpdateRequest`

**Key file: `web_media_player_impl.cc:1697`**

The DCHECK is satisfied after Phase 1 because `"fairplay"` maps to `FAIRPLAY`, not `UNKNOWN`.

### Phase 3: Init Data Sanitization (optional, for robustness)

WebKit has dedicated sanitize/extract functions for each FairPlay init data type.
This is optional but would make the implementation more robust.

**File: `media/cdm/cdm_type_conversion.cc` or new file**

Add sanitizers:
- `SINF`: Validate sinf box structure, extract key IDs from sinf atoms
- `SKD`: Validate skd:// URI format, extract key ID from URI
- `FAIRPLAY`: Validate packed skd URI format from AVPlayer

Without Phase 3, the init data passes through unsanitized (same as C25 behavior).

## 3. Recommended Order

| Phase | Scope | Risk | Files Changed |
|---|---|---|---|
| **Phase 1** | Enum + string mapping + switch updates | Low (additive, no behavior change for existing key systems) | ~15 files |
| **Phase 2** | Verify playback path works end-to-end | Medium (depends on Phase 1) | 0 files (just testing) |
| **Phase 3** | Init data sanitization | Low (optional hardening) | 1-2 files |

## 4. What We Can Skip

- **`"mpts"` support** - Only in WebKit behind conditional flag, not used by our streams
- **`"cenc"` for FairPlay init data** - Our FairPlay streams use `skd`/`sinf`, not PSSH boxes
- **Modifying upstream Chromium flow** - All changes are additive enum values and switch cases

## 5. Testing Strategy

**Phase 1 test:**
- `requestMediaKeySystemAccess("com.youtube.fairplay", [{ initDataTypes: ['sinf', 'skd'] }])` should succeed
- `requestMediaKeySystemAccess("com.youtube.fairplay", [{ initDataTypes: ['cenc'] }])` should fail (FairPlay doesn't use PSSH)
- Widevine/ClearKey should be unaffected

**Phase 2 test:**
- Load FairPlay HLS stream, verify `encrypted` event fires with `initDataType: "fairplay"`
- Verify `session.generateRequest("fairplay", initData)` reaches `SbDrmGenerateSessionUpdateRequest`
- No DCHECK crash

## 6. Build Flag Guard

All FairPlay enum additions should be unconditional (they're just enum values and string
mappings). The FairPlay-specific behavior is already guarded by `IS_IOS_TVOS &&
USE_STARBOARD_MEDIA` at the `FairplayKeySystemInfo` registration site.

Adding the enum values unconditionally is consistent with how Chromium handles platform-specific
DRM - the enum is shared, but the KeySystemInfo registration is platform-guarded.
