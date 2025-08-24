// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_MEDIA_SERVICE_VIDEO_GEOMETRY_SETTER_SERVICE_H_
#define COBALT_MEDIA_SERVICE_VIDEO_GEOMETRY_SETTER_SERVICE_H_

#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/unguessable_token.h"
#include "cobalt/media/service/mojom/video_geometry_setter.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/receiver_set.h"

namespace base {
class SequencedTaskRunner;
}  // namespace base

namespace cobalt {
namespace media {

// VideoGeometrySetterService: Central N:M Broker for Punch-Out Video Geometry
// ============================================================================
// 
// CRITICAL ARCHITECTURE: This service implements the N:M broker pattern for 
// coordinating video geometry between multiple Compositor threads (GPU Process) 
// and multiple Video Renderer clients (Renderer Process).
//
// DUAL ROLE IMPLEMENTATION:
// 1. VideoGeometryChangeSubscriber: Receives SUBSCRIPTIONS from VideoGeometryChangeClients
// 2. VideoGeometrySetter: Receives GEOMETRY UPDATES from overlay strategy classes
//
// N:M COORDINATION PATTERN:
// 
// MULTIPLE VideoGeometryChangeClients (N clients that RECEIVE geometry):
// - StarboardRendererClient (Cobalt video playback)
// - CastRenderer (Chromecast video playback)  
// - Multiple instances for multiple video elements on same page
// - Each client identified by unique overlay_plane_id
//
// MULTIPLE VideoGeometrySetters (M setters that SEND geometry):
// - OverlayStrategyUnderlayStarboard (Cobalt compositor in GPU Process)
// - OverlayStrategyUnderlayCast (Chromecast compositor in GPU Process)
// - Generally limited to one active setter per platform
//
// BROKER RESPONSIBILITIES:
// 1. Maintain mapping: overlay_plane_id -> VideoGeometryChangeClient
// 2. Forward geometry updates from any setter to correct client(s)
// 3. Handle client lifecycle (connect/disconnect)
// 4. Prevent conflicts between multiple video streams
// 5. Ensure proper cleanup when clients disconnect
//
// WHY BROWSER PROCESS BROKER (instead of direct communication):
// - Centralized coordination prevents geometry conflicts
// - Single source of truth for video positioning
// - Proper multiplexing for multiple video elements
// - Lifecycle management and cleanup coordination
// - Cross-platform compatibility (Cobalt + Chromecast)
//
// PROCESS ARCHITECTURE:
// GPU Process (Compositor) → Browser Process (THIS BROKER) → Renderer Process (Video Client)
//
// This service runs and destructs on the sequence where it's constructed, but
// the public methods can be run on any sequence.
class VideoGeometrySetterService final
    : public mojom::VideoGeometryChangeSubscriber,  // RECEIVES subscriptions from clients
      public mojom::VideoGeometrySetter {           // RECEIVES geometry from compositors
 public:
  VideoGeometrySetterService();

  VideoGeometrySetterService(const VideoGeometrySetterService&) = delete;
  VideoGeometrySetterService& operator=(const VideoGeometrySetterService&) =
      delete;

  ~VideoGeometrySetterService() override;

  void GetVideoGeometryChangeSubscriber(
      mojo::PendingReceiver<mojom::VideoGeometryChangeSubscriber>
          pending_receiver);
  void GetVideoGeometrySetter(
      mojo::PendingReceiver<mojom::VideoGeometrySetter> pending_receiver);
  base::RepeatingCallback<
      void(mojo::PendingReceiver<mojom::VideoGeometryChangeSubscriber>)>
  GetBindSubscriberCallback();

 private:
  // mojom::VideoGeometryChangeSubscriber implementation.
  void SubscribeToVideoGeometryChange(
      const base::UnguessableToken& overlay_plane_id,
      mojo::PendingRemote<mojom::VideoGeometryChangeClient>
          client_pending_remote,
      SubscribeToVideoGeometryChangeCallback callback) override;
  // mojom::VideoGeometrySetter implementation.
  void SetVideoGeometry(
      const gfx::RectF& rect_f,
      gfx::OverlayTransform transform,
      const base::UnguessableToken& overlay_plane_id) override;

  void OnVideoGeometryChangeClientGone(
      const base::UnguessableToken overlay_plane_id);

  const scoped_refptr<base::SequencedTaskRunner> task_runner_;

  // CRITICAL N:M BROKER DATA STRUCTURE: overlay_plane_id → VideoGeometryChangeClient mapping
  //
  // This flat_map is the CORE of the N:M broker pattern implementation:
  // - KEY: overlay_plane_id (unique identifier for each video element)
  // - VALUE: mojo::Remote<VideoGeometryChangeClient> (connection to video renderer)
  //
  // ENABLES:
  // - Multiple video elements on same page (each gets unique overlay_plane_id)
  // - Cross-platform video clients (StarboardRendererClient + CastRenderer)
  // - Proper geometry routing without cross-contamination
  // - Lifecycle management (clients can connect/disconnect independently)
  //
  // USAGE PATTERN:
  // 1. Client calls SubscribeToVideoGeometryChange() → entry added to map
  // 2. Compositor calls SetVideoGeometry() → lookup in map by overlay_plane_id
  // 3. Geometry forwarded to correct client via map lookup
  // 4. Client disconnects → entry removed from map
  base::flat_map<base::UnguessableToken,
                 mojo::Remote<mojom::VideoGeometryChangeClient>>
      video_geometry_change_clients_;

  // MULTIPLE SUBSCRIPTION RECEIVERS: Handle N VideoGeometryChangeClients registering
  mojo::ReceiverSet<mojom::VideoGeometryChangeSubscriber>
      video_geometry_change_subscriber_receivers_;
      
  // SINGLE GEOMETRY RECEIVER: Handle M VideoGeometrySetters sending updates  
  // (Generally limited to one active setter per platform to prevent conflicts)
  mojo::Receiver<mojom::VideoGeometrySetter> video_geometry_setter_receiver_{
      this};

  base::WeakPtrFactory<VideoGeometrySetterService> weak_factory_{this};
};

}  // namespace media
}  // namespace cobalt

#endif  // COBALT_MEDIA_SERVICE_VIDEO_GEOMETRY_SETTER_SERVICE_H_
