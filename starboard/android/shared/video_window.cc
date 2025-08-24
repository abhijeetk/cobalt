// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/android/shared/video_window.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <mutex>

#include "starboard/android/shared/jni_env_ext.h"
#include "starboard/android/shared/starboard_bridge.h"
#include "starboard/common/log.h"
#include "starboard/common/once.h"
#include "starboard/configuration.h"
#include "starboard/shared/gles/gl_call.h"

namespace starboard::android::shared {

// TODO: (cobalt b/372559388) Update namespace to jni_zero.
using base::android::AttachCurrentThread;

namespace {

// [ABHIJEET][PUNCH-OUT] ANDROID VIDEO SURFACE ARCHITECTURE:
// Android TV platforms have EXACTLY ONE hardware video overlay surface.
// This is the fundamental reason why only one punch-out player can exist.

// Global video surface pointer mutex.
SB_ONCE_INITIALIZE_FUNCTION(std::mutex, GetViewSurfaceMutex)

// [ABHIJEET][PUNCH-OUT] THE SINGLE VIDEO HOLE:
// These globals represent the ONE AND ONLY hardware video surface on Android TV.
// When g_video_surface_holder is non-null, the surface is "occupied" by a player.

// Global pointer to the single video surface.
jobject g_j_video_surface = NULL;
// Global pointer to the single video window.
ANativeWindow* g_native_video_window = NULL;
// Global video surface pointer holder.
VideoSurfaceHolder* g_video_surface_holder = NULL;
// Global boolean to indicate if we need to reset SurfaceView after playing
// vertical video.
bool g_reset_surface_on_clear_window = false;

}  // namespace

extern "C" SB_EXPORT_PLATFORM void
Java_dev_cobalt_media_VideoSurfaceView_nativeOnVideoSurfaceChanged(
    JNIEnv* env,
    jobject unused_this,
    jobject surface) {
  std::lock_guard lock(*GetViewSurfaceMutex());
  
  // [ABHIJEET][PUNCH-OUT] CRITICAL SURFACE CHANGE NOTIFICATION
  SB_LOG(INFO) << "[ABHIJEET][PUNCH-OUT] nativeOnVideoSurfaceChanged - HARDWARE VIDEO SURFACE LIFECYCLE EVENT"
               << " | Surface Parameter: " << (surface ? "NEW_SURFACE" : "NULL_SURFACE_DESTROY")
               << " | Current Holder: " << (g_video_surface_holder ? "OCCUPIED" : "FREE")
               << " | Current Surface: " << (g_j_video_surface ? "EXISTS" : "NULL")
               << " | PURPOSE: Managing the SINGLE hardware video overlay surface lifecycle";
  
  if (g_video_surface_holder) {
    SB_LOG(INFO) << "[ABHIJEET][PUNCH-OUT] Destroying existing surface holder - player will lose hardware access";
    g_video_surface_holder->OnSurfaceDestroyed();
    g_video_surface_holder = NULL;
  }
  if (g_j_video_surface) {
    SB_LOG(INFO) << "[ABHIJEET][PUNCH-OUT] Releasing existing Java surface reference";
    env->DeleteGlobalRef(g_j_video_surface);
    g_j_video_surface = NULL;
  }
  if (g_native_video_window) {
    SB_LOG(INFO) << "[ABHIJEET][PUNCH-OUT] Releasing existing native video window";
    ANativeWindow_release(g_native_video_window);
    g_native_video_window = NULL;
  }
  if (surface) {
    SB_LOG(INFO) << "[ABHIJEET][PUNCH-OUT] Creating NEW hardware video surface - ready for punch-out video playback";
    g_j_video_surface = env->NewGlobalRef(surface);
    g_native_video_window = ANativeWindow_fromSurface(env, surface);
  }
  
  SB_LOG(INFO) << "[ABHIJEET][PUNCH-OUT] Surface change complete"
               << " | New Surface State: " << (g_j_video_surface ? "READY" : "DESTROYED")
               << " | Hardware Overlay: " << (g_native_video_window ? "AVAILABLE" : "UNAVAILABLE")
               << " | RESULT: " << (surface ? "Hardware video surface ready for painting" : "Hardware video surface destroyed");
}

extern "C" SB_EXPORT_PLATFORM void
Java_dev_cobalt_media_VideoSurfaceView_nativeSetNeedResetSurface(
    JNIEnv* env,
    jobject unused_this) {
  g_reset_surface_on_clear_window = true;
}

// static
bool VideoSurfaceHolder::IsVideoSurfaceAvailable() {
  // [ABHIJEET][PUNCH-OUT] SURFACE AVAILABILITY CHECK:
  // We only consider video surface is available when there is a video
  // surface and it is not held by any decoder, i.e.
  // g_video_surface_holder is NULL.
  //
  // IMPORTANT: This enforces the "ONE PUNCH-OUT PLAYER" rule.
  // If g_video_surface_holder != NULL, another player is using the surface.
  std::lock_guard lock(*GetViewSurfaceMutex());
  bool available = !g_video_surface_holder && g_j_video_surface;
  
  SB_LOG(INFO) << "[ABHIJEET][PUNCH-OUT] Surface availability check: "
               << "available=" << available 
               << ", holder=" << (g_video_surface_holder ? "OCCUPIED" : "FREE")
               << ", surface=" << (g_j_video_surface ? "EXISTS" : "NULL");
  
  return available;
}

jobject VideoSurfaceHolder::AcquireVideoSurface() {
  std::lock_guard lock(*GetViewSurfaceMutex());
  
  // [ABHIJEET][PUNCH-OUT] SURFACE ACQUISITION - THE CRITICAL STEP:
  // This is where the "one punch-out player" rule is enforced.
  // Only ONE VideoSurfaceHolder can own the surface at a time.
  
  if (g_video_surface_holder != NULL) {
    SB_LOG(WARNING) << "[ABHIJEET][PUNCH-OUT] Surface acquisition FAILED - already held by another player";
    return NULL;
  }
  if (!g_j_video_surface) {
    SB_LOG(WARNING) << "[ABHIJEET][PUNCH-OUT] Surface acquisition FAILED - no surface available";
    return NULL;
  }
  
  // SUCCESS: This player now owns the SINGLE hardware video surface
  g_video_surface_holder = this;
  SB_LOG(INFO) << "[ABHIJEET][PUNCH-OUT] Surface acquisition SUCCESS - player now owns the hardware overlay";
  
  return g_j_video_surface;
}

void VideoSurfaceHolder::ReleaseVideoSurface() {
  std::lock_guard lock(*GetViewSurfaceMutex());
  if (g_video_surface_holder == this) {
    // [ABHIJEET][PUNCH-OUT] SURFACE RELEASE:
    // Freeing the SINGLE hardware video surface so another player can use it.
    // This is critical for proper resource management.
    SB_LOG(INFO) << "[ABHIJEET][PUNCH-OUT] Surface released - hardware overlay now available for other players";
    g_video_surface_holder = NULL;
  } else {
    SB_LOG(WARNING) << "[ABHIJEET][PUNCH-OUT] Surface release called but this holder doesn't own the surface";
  }
}

bool VideoSurfaceHolder::GetVideoWindowSize(int* width, int* height) {
  std::lock_guard lock(*GetViewSurfaceMutex());
  if (g_native_video_window == NULL) {
    return false;
  } else {
    *width = ANativeWindow_getWidth(g_native_video_window);
    *height = ANativeWindow_getHeight(g_native_video_window);
    return true;
  }
}

void VideoSurfaceHolder::ClearVideoWindow(bool force_reset_surface) {
  // Lock *GetViewSurfaceMutex() here, to avoid releasing g_native_video_window
  // during painting.
  std::lock_guard lock(*GetViewSurfaceMutex());

  if (!g_native_video_window) {
    SB_LOG(INFO) << "Tried to clear video window when it was null.";
    return;
  }

  JNIEnv* env = AttachCurrentThread();
  if (!env) {
    SB_LOG(INFO) << "Tried to clear video window when JNIEnv was null.";
    return;
  }

  if (force_reset_surface) {
    StarboardBridge::GetInstance()->ResetVideoSurface(env);
    SB_LOG(INFO) << "Video surface has been reset.";
    return;
  } else if (g_reset_surface_on_clear_window) {
    int width = ANativeWindow_getWidth(g_native_video_window);
    int height = ANativeWindow_getHeight(g_native_video_window);
    if (width <= height) {
      StarboardBridge::GetInstance()->ResetVideoSurface(env);
      return;
    }
  }
}

}  // namespace starboard::android::shared
