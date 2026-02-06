# Widevine Integration Analysis: Android vs Linux

Complete analysis of how Widevine DRM is integrated into Cobalt builds for Android and Linux platforms.

**Date:** 2026-01-02
**Analyzed Platforms:** Android (x86), Linux (x64)

---

## Table of Contents

1. [Build Configuration](#1-build-configuration)
2. [Architecture Comparison](#2-architecture-comparison)
3. [DRM System Creation](#3-drm-system-creation)
4. [Key System Validation](#4-key-system-validation)
5. [Media Support Checking](#5-media-support-checking)
6. [Build Dependencies](#6-build-dependencies)
7. [Key Differences Summary](#7-key-differences-summary)
8. [Runtime Flow Comparison](#8-runtime-flow-comparison)
9. [Important Files Reference](#9-important-files-reference)
10. [Build System Integration](#10-build-system-integration)

---

## 1. Build Configuration

### Android
**File:** `starboard/android/shared/platform_configuration/configuration.gni:32`
```gni
sb_widevine_platform = "android"
```

**Build Command:**
```bash
ninja -C out/android-x86_debug cobalt_apk
```

### Linux
**File:** `starboard/linux/shared/platform_configuration/configuration.gni:35`
```gni
sb_widevine_platform = "linux"
```

**Build Command:**
```bash
ninja -C out/linux-x64_debug cobalt
```

---

## 2. Architecture Comparison

### Android Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Cobalt Application                    │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              Starboard DRM API Layer                     │
│  (starboard/android/shared/drm_create_system.cc)        │
│  Creates: DrmSystem (Android-specific)                   │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│        Native Bridge (JNI) - media_drm_bridge.cc        │
│  Calls Java MediaDrmBridge via JNI                      │
└────────────────────┬────────────────────────────────────┘
                     │ JNI
┌────────────────────▼────────────────────────────────────┐
│  Java MediaDrmBridge.java                               │
│  UUID: edef8ba9-79d6-4ace-a3c8-27dcd51d21ed             │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│         Android Framework MediaDrm API                   │
│  (android.media.MediaDrm, MediaCrypto)                  │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│    Device-Specific Widevine DRM Implementation          │
│    (Provided by device manufacturer/OEM)                │
└─────────────────────────────────────────────────────────┘
```

### Linux Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Cobalt Application                    │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              Starboard DRM API Layer                     │
│  (starboard/linux/shared/drm_create_system.cc)          │
│  Creates: DrmSystemWidevine                              │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│    DrmSystemWidevine (drm_system_widevine.cc/.h)        │
│    Implements widevine::Cdm::IEventListener             │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│      Widevine CE CDM v3.5 Library                       │
│  (third_party/internal/ce_cdm/cdm:widevine_ce_cdm_*)   │
│  Direct C++ API: widevine::Cdm                          │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│       OEMCrypto Implementation                           │
│  (starboard/shared/widevine:oemcrypto)                  │
│  + Keybox (platform-specific)                           │
└─────────────────────────────────────────────────────────┘
```

---

## 3. DRM System Creation

### Android Implementation

**File:** `starboard/android/shared/drm_create_system.cc:18-48`

```cpp
SbDrmSystem SbDrmCreateSystem(
    const char* key_system,
    void* context,
    SbDrmSessionUpdateRequestFunc update_request_callback,
    SbDrmSessionUpdatedFunc session_updated_callback,
    SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback,
    SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback,
    SbDrmSessionClosedFunc session_closed_callback) {
  using starboard::DrmSystem;
  using starboard::IsWidevineL1;
  using starboard::IsWidevineL3;

  // Validate callbacks
  if (!update_request_callback || !session_updated_callback ||
      !key_statuses_changed_callback || !server_certificate_updated_callback ||
      !session_closed_callback) {
    return kSbDrmSystemInvalid;
  }

  // Validate key system
  if (!IsWidevineL1(key_system) && !IsWidevineL3(key_system)) {
    return kSbDrmSystemInvalid;
  }

  // Create Android-specific DRM system
  DrmSystem* drm_system =
      new DrmSystem(key_system, context, update_request_callback,
                    session_updated_callback, key_statuses_changed_callback);
  if (!drm_system->is_valid()) {
    delete drm_system;
    return kSbDrmSystemInvalid;
  }
  return drm_system;
}
```

**Supported Key Systems:**
- `com.widevine` (L1)
- `com.widevine.alpha` (L1)
- `com.youtube.widevine.l3` (L3)

### Linux Implementation

**File:** `starboard/linux/shared/drm_create_system.cc:20-46`

```cpp
SbDrmSystem SbDrmCreateSystem(
    const char* key_system,
    void* context,
    SbDrmSessionUpdateRequestFunc update_request_callback,
    SbDrmSessionUpdatedFunc session_updated_callback,
    SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback,
    SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback,
    SbDrmSessionClosedFunc session_closed_callback) {
  using starboard::DrmSystemWidevine;

  // Validate callbacks
  if (!update_request_callback || !session_updated_callback) {
    return kSbDrmSystemInvalid;
  }
  if (!key_statuses_changed_callback) {
    return kSbDrmSystemInvalid;
  }
  if (!server_certificate_updated_callback || !session_closed_callback) {
    return kSbDrmSystemInvalid;
  }

  // Check key system support
  if (!DrmSystemWidevine::IsKeySystemSupported(key_system)) {
    SB_DLOG(WARNING) << "Invalid key system " << key_system;
    return kSbDrmSystemInvalid;
  }

  // Create Widevine CDM wrapper
  return new DrmSystemWidevine(
      context, update_request_callback, session_updated_callback,
      key_statuses_changed_callback, server_certificate_updated_callback,
      session_closed_callback, "Linux", "Linux");
}
```

**Supported Key Systems:**
- `com.widevine`
- `com.widevine.alpha`
- With encryption schemes: `cenc`, `cbcs`, `cbcs-1-9`

---

## 4. Key System Validation

### Android Implementation

**File:** `starboard/android/shared/media_common.h:29-35`

```cpp
inline bool IsWidevineL1(const char* key_system) {
  return strcmp(key_system, "com.widevine") == 0 ||
         strcmp(key_system, "com.widevine.alpha") == 0;
}

inline bool IsWidevineL3(const char* key_system) {
  return strcmp(key_system, "com.youtube.widevine.l3") == 0;
}
```

**Simple string comparison** to identify Widevine L1 vs L3 key systems.

### Linux Implementation

**File:** `starboard/shared/widevine/drm_system_widevine.cc:268-295`

```cpp
// static
bool DrmSystemWidevine::IsKeySystemSupported(const char* key_system) {
  SB_DCHECK(key_system);

  // It is possible that the |key_system| comes with extra attributes, like
  // `com.widevine.alpha; encryptionscheme="cenc"`.  We prepend "key_system/"
  // to it, so it can be parsed by MimeType.
  starboard::MimeType mime_type(std::string("key_system/") + key_system);

  if (!mime_type.is_valid()) {
    return false;
  }
  SB_DCHECK_EQ(mime_type.type(), "key_system");

  const char* kWidevineKeySystems[] = {"com.widevine", "com.widevine.alpha"};

  for (auto wv_key_system : kWidevineKeySystems) {
    if (mime_type.subtype() == wv_key_system) {
      // Validate encryption scheme parameters
      for (int i = 0; i < mime_type.GetParamCount(); ++i) {
        if (mime_type.GetParamName(i) == "encryptionscheme") {
          auto value = mime_type.GetParamStringValue(i);
          if (value != "cenc" && value != "cbcs" && value != "cbcs-1-9") {
            return false;
          }
        }
      }
      return true;
    }
  }
  return false;
}
```

**MIME-type parsing** to handle key system strings with parameters (e.g., encryption schemes).

---

## 5. Media Support Checking

### Android Implementation

**File:** `starboard/android/shared/media_capabilities_cache.cc:305-311`

```cpp
bool MediaCapabilitiesCache::IsWidevineSupported() {
  if (is_enabled_) {
    return media_capabilities_provider_->GetIsWidevineSupported();
  }
  // If cache disabled, return cached value
  return is_widevine_supported_;
}
```

**Provider Implementation:** `starboard/android/shared/media_capabilities_cache.cc:88-90`

```cpp
bool GetIsWidevineSupported() override {
  return MediaDrmBridge::IsWidevineSupported(AttachCurrentThread());
}
```

**Native Bridge:** `starboard/android/shared/media_drm_bridge.cc:315-316`

```cpp
bool MediaDrmBridge::IsWidevineSupported(JNIEnv* env) {
  return Java_MediaDrmBridge_isWidevineCryptoSchemeSupported(env) == JNI_TRUE;
}
```

**Java Implementation:** `cobalt/android/.../MediaDrmBridge.java:189-191`

```java
@CalledByNative
static boolean isWidevineCryptoSchemeSupported() {
  return MediaDrm.isCryptoSchemeSupported(WIDEVINE_UUID);
}

// Line 75: Widevine UUID constant
private static final UUID WIDEVINE_UUID =
    UUID.fromString("edef8ba9-79d6-4ace-a3c8-27dcd51d21ed");
```

### Linux Implementation

**File:** `starboard/shared/widevine/media_is_supported.cc:21-25`

```cpp
namespace starboard::shared::starboard::media {

bool MediaIsSupported(SbMediaVideoCodec video_codec,
                      SbMediaAudioCodec audio_codec,
                      const char* key_system) {
  return DrmSystemWidevine::IsKeySystemSupported(key_system);
}

}  // namespace starboard::shared::starboard::media
```

**Direct validation** via `DrmSystemWidevine::IsKeySystemSupported()`.

---

## 6. Build Dependencies

### Android Build Chain

**File:** `starboard/android/shared/BUILD.gn:35-347`

```gn
static_library("starboard_platform") {
  sources = [
    # DRM-related sources
    "drm_create_system.cc",          # Line 183
    "drm_system.cc",                 # Line 186
    "drm_system.h",                  # Line 187
    "media_drm_bridge.cc",           # Line 211
    "media_drm_bridge.h",            # Line 212
    "media_is_supported.cc",         # Line 219
    "media_capabilities_cache.cc",   # Line 204
    "media_capabilities_cache.h",    # Line 205
    "media_common.h",                # Line 208

    # Other platform sources...
  ]

  public_deps = [
    "//cobalt/android:jni_headers",
    "//starboard/common",
    "//starboard/shared/starboard/media:media_util",
    "//starboard/shared/starboard/player/filter:filter_based_player_sources",
  ]

  deps = [
    "//base",
    "//third_party/opus",
  ]

  # Note: NO oemcrypto library dependency
  # Android uses the MediaDrm framework built into the OS
}
```

**Dependency Chain:**
```
cobalt_apk (cobalt/android/BUILD.gn:238)
  └─> libchrobalt (cobalt/android/BUILD.gn:196)
      └─> //cobalt/browser
          └─> starboard_platform (starboard/android/shared/BUILD.gn:35)
              ├─> drm_create_system.cc
              ├─> drm_system.cc
              ├─> media_drm_bridge.cc (JNI to Java)
              └─> Java MediaDrmBridge.java
                  └─> Android MediaDrm API
```

### Linux Build Chain

**File:** `starboard/linux/shared/BUILD.gn:265-304`

```gn
static_library("starboard_platform") {
  # ... base sources ...

  if (is_internal_build) {
    sources += [
      # Platform-specific DRM implementation
      "//internal/starboard/linux/shared/internal/oemcrypto_engine_device_properties_linux.cc",
      "//starboard/linux/shared/drm_create_system.cc",                # Line 268

      # Shared Widevine implementation
      "//starboard/shared/widevine/drm_system_widevine.cc",          # Line 279
      "//starboard/shared/widevine/drm_system_widevine.h",           # Line 280
      "//starboard/shared/widevine/media_is_supported.cc",           # Line 281
      "//starboard/shared/widevine/widevine_storage.cc",             # Line 282
      "//starboard/shared/widevine/widevine_storage.h",              # Line 283
      "//starboard/shared/widevine/widevine_timer.cc",               # Line 284
      "//starboard/shared/widevine/widevine_timer.h",                # Line 285

      # Common DRM sources
      "//starboard/shared/starboard/drm/drm_close_session.cc",       # Line 271
      "//starboard/shared/starboard/drm/drm_destroy_system.cc",      # Line 272
      "//starboard/shared/starboard/drm/drm_generate_session_update_request.cc",
      "//starboard/shared/starboard/drm/drm_get_metrics.cc",
      "//starboard/shared/starboard/drm/drm_is_server_certificate_updatable.cc",
      "//starboard/shared/starboard/drm/drm_system_internal.h",
      "//starboard/shared/starboard/drm/drm_update_server_certificate.cc",
      "//starboard/shared/starboard/drm/drm_update_session.cc",

      # Device authentication
      "//starboard/shared/deviceauth/deviceauth_internal.cc",
      "//starboard/shared/deviceauth/deviceauth_internal.h",
    ]

    deps += [
      "//starboard/shared/widevine:oemcrypto",                       # Line 288
      "//third_party/boringssl",                                     # Line 289
      "//third_party/internal/ce_cdm/cdm:widevine_ce_cdm_static",  # Line 290
    ]
  } else {
    # Stub implementations if not internal build
    sources += [
      "//starboard/shared/stub/drm_close_session.cc",
      "//starboard/shared/stub/drm_create_system.cc",
      # ... other stubs ...
    ]
  }
}
```

**OEMCrypto Static Library:** `starboard/shared/widevine/BUILD.gn:30-51`

```gn
config("oemcrypto_external") {
  include_dirs = [
    "//third_party/internal/ce_cdm/core/include",
    "//third_party/internal/ce_cdm/oemcrypto/include",
  ]
}

config("oemcrypto_internal") {
  defines = [
    "COBALT_WIDEVINE_KEYBOX_TRANSFORM_FUNCTION=${sb_widevine_platform}_client",
    "COBALT_WIDEVINE_KEYBOX_TRANSFORM_INCLUDE=\"starboard/keyboxes/${sb_widevine_platform}/${sb_widevine_platform}.h\"",
    "COBALT_WIDEVINE_KEYBOX_INCLUDE=\"starboard/keyboxes/${sb_widevine_platform}_widevine_keybox.h\"",
  ]
}

static_library("oemcrypto") {
  sources = [
    "//internal/starboard/shared/widevine/internal/wv_keybox.cc",
    "//starboard/keyboxes/${sb_widevine_platform}/${sb_widevine_platform}.h",
    "//starboard/keyboxes/${sb_widevine_platform}/${sb_widevine_platform}_client.c",
    "//starboard/shared/widevine/widevine_keybox_hash.cc",
  ]

  public_configs = [ ":oemcrypto_external" ]
  configs -= [ "//starboard/build/config:size" ]
  configs += [
    ":oemcrypto_internal",
    "//starboard/build/config:speed",
    "//third_party/internal/ce_cdm/cdm:shared_config",
  ]

  deps = [
    "//starboard/common",
    "//third_party/boringssl",
    "//third_party/internal/ce_cdm/oemcrypto/mock:oec_mock",
  ]
}
```

**Dependency Chain:**
```
cobalt (target)
  └─> starboard_platform (starboard/linux/shared/BUILD.gn)
      ├─> drm_create_system.cc
      ├─> drm_system_widevine.cc (Widevine CDM adapter)
      ├─> //starboard/shared/widevine:oemcrypto
      │   ├─> wv_keybox.cc
      │   ├─> keybox files (platform-specific)
      │   └─> widevine_keybox_hash.cc
      └─> //third_party/internal/ce_cdm/cdm:widevine_ce_cdm_static
          └─> Widevine CE CDM v3.5 library
```

---

## 7. Key Differences Summary

| Aspect | Android | Linux |
|--------|---------|-------|
| **DRM Implementation** | Uses Android Framework MediaDrm API | Uses Widevine CE CDM library directly |
| **Language/Layer** | Java (JNI bridge to native) | Pure C++ |
| **DRM Class** | `starboard::DrmSystem` | `starboard::DrmSystemWidevine` |
| **Key System Check** | `MediaDrm.isCryptoSchemeSupported()` | `DrmSystemWidevine::IsKeySystemSupported()` |
| **Widevine CDM** | Built into Android OS | Linked statically: `widevine_ce_cdm_static` |
| **OEMCrypto** | Provided by device manufacturer | Built from `starboard/shared/widevine:oemcrypto` |
| **Keybox** | Managed by Android OS | Platform-specific: `starboard/keyboxes/linux/` |
| **Security Level** | L1 (hardware) or L3 (software) | Depends on OEMCrypto implementation |
| **Dependencies** | Android MediaDrm, MediaCrypto | Widevine CDM, OEMCrypto, BoringSSL |
| **is_internal_build** | Not required | **Required** (Widevine is proprietary) |
| **Encryption Schemes** | Handled by MediaDrm | `cenc`, `cbcs`, `cbcs-1-9` |
| **Session Management** | Java MediaDrm API | C++ widevine::Cdm API |
| **Event Listener** | Java callbacks via JNI | C++ `widevine::Cdm::IEventListener` |

---

## 8. Runtime Flow Comparison

### Android Runtime Flow

```
1. App calls SbMediaCanPlayMimeAndKeySystem("com.widevine.alpha", ...)
   └─> starboard/android/shared/media_is_supported.cc
       └─> MediaCapabilitiesCache::IsWidevineSupported()
           └─> MediaCapabilitiesProvider::GetIsWidevineSupported()
               └─> MediaDrmBridge::IsWidevineSupported(JNIEnv*)
                   └─> [JNI Call]
                       └─> MediaDrmBridge.isWidevineCryptoSchemeSupported()
                           └─> MediaDrm.isCryptoSchemeSupported(WIDEVINE_UUID)
                               └─> Android Framework checks device DRM capabilities

2. App calls SbDrmCreateSystem("com.widevine.alpha", ...)
   └─> starboard/android/shared/drm_create_system.cc
       └─> Validate key system: IsWidevineL1() or IsWidevineL3()
           └─> new DrmSystem(key_system, callbacks...)
               └─> DrmSystem::Initialize()
                   └─> [JNI Call]
                       └─> MediaDrmBridge.create()
                           └─> new MediaDrm(WIDEVINE_UUID)

3. Session Management
   App → SbDrmGenerateSessionUpdateRequest()
       → DrmSystem::GenerateSessionUpdateRequest()
           → [JNI] MediaDrmBridge.createSession()
               → MediaDrm.openSession()
                   → Device DRM implementation
```

### Linux Runtime Flow

```
1. App calls SbMediaCanPlayMimeAndKeySystem("com.widevine.alpha", ...)
   └─> starboard/shared/widevine/media_is_supported.cc
       └─> DrmSystemWidevine::IsKeySystemSupported()
           └─> Parse MIME-type format key system
               └─> Validate against kWidevineKeySystems[]
                   └─> Check encryption scheme parameters

2. App calls SbDrmCreateSystem("com.widevine.alpha", ...)
   └─> starboard/linux/shared/drm_create_system.cc
       └─> Validate: DrmSystemWidevine::IsKeySystemSupported()
           └─> new DrmSystemWidevine(callbacks..., "Linux", "Linux")
               └─> DrmSystemWidevine::DrmSystemWidevine()
                   ├─> Initialize Widevine storage (wvcdm.dat)
                   ├─> widevine::Cdm::create(this, storage, privacy_mode)
                   │   └─> Load OEMCrypto library
                   │       └─> Initialize platform keybox
                   └─> Set certification scope parameter

3. Session Management
   App → SbDrmGenerateSessionUpdateRequest()
       → DrmSystemWidevine::GenerateSessionUpdateRequest()
           → widevine::Cdm::createSession()
               → OEMCrypto operations
                   → widevine::Cdm::IEventListener::onMessage()
                       → Callback to app
```

---

## 9. Important Files Reference

### Android Platform Files

| File | Line | Purpose |
|------|------|---------|
| starboard/android/shared/platform_configuration/configuration.gni | 32 | Platform config: `sb_widevine_platform = "android"` |
| starboard/android/shared/BUILD.gn | 35-347 | Starboard platform library definition |
| starboard/android/shared/drm_create_system.cc | 18-48 | DRM system creation entry point |
| starboard/android/shared/drm_system.h | 85 | DrmSystem class with `IsWidevineL1()` check |
| starboard/android/shared/drm_system.cc | - | DrmSystem implementation |
| starboard/android/shared/media_drm_bridge.cc | 315-316 | JNI bridge: `IsWidevineSupported()` |
| starboard/android/shared/media_drm_bridge.h | 106 | MediaDrmBridge class declaration |
| starboard/android/shared/media_common.h | 29-35 | Key system validators: `IsWidevineL1()`, `IsWidevineL3()` |
| starboard/android/shared/media_is_supported.cc | 47-52 | Media support validation |
| starboard/android/shared/media_capabilities_cache.cc | 305-311, 500-501 | Widevine support caching |
| starboard/android/shared/media_capabilities_cache.h | 121, 147 | Cache interface definitions |
| cobalt/android/apk/app/src/main/java/dev/cobalt/media/MediaDrmBridge.java | 75, 189-191 | Java MediaDrm wrapper, Widevine UUID |
| cobalt/android/BUILD.gn | 195-236 | `libchrobalt` shared library definition |
| cobalt/android/BUILD.gn | 238-258 | `cobalt_apk` target definition |

### Linux Platform Files

| File | Line | Purpose |
|------|------|---------|
| starboard/linux/shared/platform_configuration/configuration.gni | 35 | Platform config: `sb_widevine_platform = "linux"` |
| starboard/linux/shared/BUILD.gn | 265-304 | Platform library with Widevine integration |
| starboard/linux/shared/drm_create_system.cc | 20-46 | DRM system creation for Linux |

### Shared Widevine Implementation Files

| File | Line | Purpose |
|------|------|---------|
| starboard/shared/widevine/BUILD.gn | 15-51 | OEMCrypto library build definition |
| starboard/shared/widevine/drm_system_widevine.h | 39-201 | `DrmSystemWidevine` class declaration |
| starboard/shared/widevine/drm_system_widevine.cc | 207-299 | `DrmSystemWidevine` implementation |
| starboard/shared/widevine/drm_system_widevine.cc | 268-295 | `IsKeySystemSupported()` implementation |
| starboard/shared/widevine/media_is_supported.cc | 21-25 | Media support check for Widevine |
| starboard/shared/widevine/widevine_storage.cc | - | CDM storage backend implementation |
| starboard/shared/widevine/widevine_storage.h | - | Storage interface for Widevine CDM |
| starboard/shared/widevine/widevine_timer.cc | - | Timer implementation for CDM |
| starboard/shared/widevine/widevine_timer.h | - | Timer interface |
| starboard/shared/widevine/widevine_keybox_hash.cc | - | Keybox hashing utilities |
| starboard/shared/widevine/widevine_keybox_hash.h | - | Keybox hash interface |

### Common Starboard DRM Files

| File | Purpose |
|------|---------|
| starboard/shared/starboard/drm/drm_system_internal.h | Base DRM system interface |
| starboard/shared/starboard/drm/drm_close_session.cc | Session closing implementation |
| starboard/shared/starboard/drm/drm_destroy_system.cc | DRM system destruction |
| starboard/shared/starboard/drm/drm_generate_session_update_request.cc | Session update request |
| starboard/shared/starboard/drm/drm_get_metrics.cc | DRM metrics retrieval |
| starboard/shared/starboard/drm/drm_update_server_certificate.cc | Server certificate update |
| starboard/shared/starboard/drm/drm_update_session.cc | Session update |

---

## 10. Build System Integration

### sb_widevine_platform Variable Usage

Both platforms use the `sb_widevine_platform` GN variable to configure platform-specific behavior:

**In OEMCrypto Build:** `starboard/shared/widevine/BUILD.gn:22-28`

```gn
config("oemcrypto_internal") {
  defines = [
    "COBALT_WIDEVINE_KEYBOX_TRANSFORM_FUNCTION=${sb_widevine_platform}_client",
    "COBALT_WIDEVINE_KEYBOX_TRANSFORM_INCLUDE=\"starboard/keyboxes/${sb_widevine_platform}/${sb_widevine_platform}.h\"",
    "COBALT_WIDEVINE_KEYBOX_INCLUDE=\"starboard/keyboxes/${sb_widevine_platform}_widevine_keybox.h\"",
  ]
}
```

**For Android** (`sb_widevine_platform = "android"`):
- `COBALT_WIDEVINE_KEYBOX_TRANSFORM_FUNCTION=android_client`
- `COBALT_WIDEVINE_KEYBOX_TRANSFORM_INCLUDE="starboard/keyboxes/android/android.h"`
- `COBALT_WIDEVINE_KEYBOX_INCLUDE="starboard/keyboxes/android_widevine_keybox.h"`

**For Linux** (`sb_widevine_platform = "linux"`):
- `COBALT_WIDEVINE_KEYBOX_TRANSFORM_FUNCTION=linux_client`
- `COBALT_WIDEVINE_KEYBOX_TRANSFORM_INCLUDE="starboard/keyboxes/linux/linux.h"`
- `COBALT_WIDEVINE_KEYBOX_INCLUDE="starboard/keyboxes/linux_widevine_keybox.h"`

### is_internal_build Requirement

**Linux** requires `is_internal_build = true` to include Widevine support because:
1. Widevine CDM library is proprietary (located in `third_party/internal/ce_cdm/`)
2. OEMCrypto implementation is confidential
3. Platform-specific keyboxes are secret

**Android** does NOT require `is_internal_build` because:
1. Widevine is provided by the Android OS
2. DRM is handled through public Android APIs (`android.media.MediaDrm`)
3. Keyboxes are managed by the device manufacturer

### Build Targets Summary

**Android:**
```bash
# Main APK target
ninja -C out/android-x86_debug cobalt_apk

# What gets built:
# - libchrobalt.so (native shared library)
# - Cobalt.apk (Android application package)
# - Java classes including MediaDrmBridge
# - JNI bindings
```

**Linux:**
```bash
# Main executable target (requires is_internal_build=true)
ninja -C out/linux-x64_debug cobalt

# What gets built:
# - cobalt executable
# - libwidevine_ce_cdm.a (Widevine CDM static library)
# - liboemcrypto.a (OEMCrypto implementation)
# - Platform-specific keybox objects
```

---

## Appendix A: Widevine Key System Strings

### Android Supported Key Systems

| Key System String | Security Level | Description |
|-------------------|----------------|-------------|
| `com.widevine` | L1 or L3 | Generic Widevine (level determined by device) |
| `com.widevine.alpha` | L1 or L3 | Widevine alpha (same as `com.widevine`) |
| `com.youtube.widevine.l3` | L3 | YouTube-specific L3 (software-based) |

### Linux Supported Key Systems

| Key System String | Description |
|-------------------|-------------|
| `com.widevine` | Standard Widevine key system |
| `com.widevine.alpha` | Widevine alpha version |
| `com.widevine; encryptionscheme="cenc"` | With CENC encryption |
| `com.widevine.alpha; encryptionscheme="cbcs"` | With CBCS encryption |
| `com.widevine.alpha; encryptionscheme="cbcs-1-9"` | With CBCS-1-9 encryption |

---

## Appendix B: Widevine UUID

**Standard Widevine UUID:** `edef8ba9-79d6-4ace-a3c8-27dcd51d21ed`

This UUID is defined in:
- Android: `cobalt/android/apk/app/src/main/java/dev/cobalt/media/MediaDrmBridge.java:75`
- Standard: http://dashif.org/identifiers/protection/

Used by Android's `MediaDrm.isCryptoSchemeSupported(UUID)` to check Widevine availability.

---

## Appendix C: Third-Party Dependencies

### Android Dependencies

- **Android Framework APIs:**
  - `android.media.MediaDrm`
  - `android.media.MediaCrypto`
  - `android.media.DeniedByServerException`
  - `android.media.NotProvisionedException`
  - `android.media.UnsupportedSchemeException`

- **Build Dependencies:**
  - JNI Zero (for JNI generation)
  - Cobalt Android JNI headers
  - Base library (Chromium base)

### Linux Dependencies

- **Widevine CE CDM:**
  - `third_party/internal/ce_cdm/cdm:widevine_ce_cdm_static`
  - `third_party/internal/ce_cdm/core/include` (headers)
  - `third_party/internal/ce_cdm/oemcrypto/include` (OEMCrypto headers)

- **OEMCrypto:**
  - `third_party/internal/ce_cdm/oemcrypto/mock:oec_mock`
  - Platform-specific keybox implementation

- **Cryptography:**
  - `third_party/boringssl` (for cryptographic operations)

- **Build Dependencies:**
  - Starboard common libraries
  - Chromium base library

---

## Conclusion

The Widevine integration in Cobalt shows two distinctly different approaches:

1. **Android** leverages the platform's built-in MediaDRM framework, using JNI to bridge between native C++ code and Java MediaDrm APIs. This approach:
   - Simplifies the integration (no need for proprietary libraries)
   - Relies on device manufacturer's Widevine implementation
   - Supports both L1 (hardware-backed) and L3 (software) security levels
   - Works on any Android device with Widevine certification

2. **Linux** uses a direct integration with Widevine's CE CDM library, requiring:
   - Proprietary Widevine CDM library (`is_internal_build = true`)
   - Custom OEMCrypto implementation with platform-specific keyboxes
   - Full C++ implementation without framework dependencies
   - More control over DRM behavior but higher integration complexity

Both implementations provide the same Starboard DRM API to upper layers, maintaining platform abstraction while allowing each platform to use its most appropriate DRM integration method.

---

**Generated:** 2026-01-02
**Analysis Tool:** Claude Code (Sonnet 4.5)
