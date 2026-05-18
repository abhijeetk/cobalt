# **Design Doc: FairPlay DRM & EME Integration on Chrobalt (Phase 2)**

*Authors: [abhijeet@igalia.com](mailto:abhijeet@igalia.com)*  
*May 2026*

# **One-page overview**

### **Summary**
This document describes our proposal to integrate Apple FairPlay Streaming (FPS) DRM into Chrobalt's multi-process architecture. Building on the Phase 1 "URL Player," this phase enables playback of protected HLS content by bridging the DRM conversation between the native platform and Chromium's EME stack.

While Phase 1 solved the process boundary problem for *routing* the URL, Phase 2 solves the more complex problem of *synchronizing* the DRM handshake. We must ensure that encrypted media events discovered by the native `AVPlayer` (GPU process) can successfully trigger the Encrypted Media Extensions (EME) logic in the web page (Renderer process).

### **Platforms**
tvOS

### **Team**
Cobalt Media Team

### **Code affected**
`media/base`, `media/starboard`, `media/filters`, `media/mojo`, `starboard/tvos/shared/media`, `third_party/blink/renderer/platform/media`, `components/cdm/renderer`

---

# **Design**

## **Problem Statement**
In Chrobalt, the media pipeline is split: `AVPlayer` lives in the GPU process, but the web page and its DRM logic (JavaScript) live in the Renderer process. For encrypted HLS, these two processes must perform a bidirectional conversation:

1.  **The GPU process** must notify the Renderer that it has discovered an encrypted stream.
2.  **The Renderer process** must request a license from a server and send it back to the GPU.
3.  **The GPU process** must apply that license to the native player to start decryption.

In the old Cobalt (C25) architecture, this was simple because everything happened in a single process using direct function calls. In Chrobalt, we must bridge this gap using Mojo IPC while ensuring that the system remains compatible with both modern web standards and legacy YouTube requirements.

## **FairPlay Integration Strategy**

### **Background: How C25 did it**
C25's FairPlay implementation was highly specialized for YouTube's private ecosystem. It relied on a custom "packed" data format that bundled the stream URL, content ID, and server certificate into a single blob. Because it was single-process, it could use direct, synchronous callbacks to pass this blob around. It also bypassed standard EME calls like `setServerCertificate()`, assuming the certificate would always be provided later inside the license request itself.

### **Why Chrobalt needs a different approach**
Our investigation shows that the legacy C25 approach is incompatible with Chrobalt’s multi-process nature for two reasons:
1.  **Process Isolation:** We can no longer rely on synchronous function calls to handle DRM events. The system must be fully asynchronous to account for Mojo IPC latency.
2.  **Standardization:** To support a broader range of HLS content (like standard Axinom or Safari-compatible streams), we must move toward standard EME behaviors. This means supporting raw URI identifiers (`skd://`) and explicit certificate handling via `setServerCertificate()`.

## **Findings from Architectural Investigation**
Through our research, we have identified several gaps that would prevent the DRM handshake from completing:

*   **Lost Encrypted Events:** Chromium expects encryption to be discovered by a "Demuxer" in the Renderer. Since our `AVPlayer` replaces the demuxer but lives in the GPU, these events are currently "orphaned"—they reach the GPU process but have no path back to the Renderer.
*   **Unsupported Data Types:** Chromium’s EME stack is designed for Common Encryption (CENC). It does not natively recognize Apple’s `SKD` or `SINF` types. Without explicit support, these events would be discarded by the pipeline before they ever reach JavaScript.
*   **Data Format Mismatch:** The native platform layer expects YouTube's custom packed blobs. When it encounters a standard `skd://` URI, it fails to "unpack" the data, causing the entire license request process to stop silently.
*   **Initialization Races:** In a multi-process browser, the video might start loading before the DRM system is fully initialized. If the system expects a perfect sequence of events, these "startup races" can lead to permanent playback timeouts.

## **Proposed Architecture**

To resolve these gaps, we propose a translation layer that safely bridges Apple's native paradigms with Chromium's multi-process requirements.

### **1. Extending the EME Type System**
We propose adding native support for Apple-specific initialization data types (`SKD` and `SINF`) directly into Chromium's core media definitions. This ensures that FairPlay-specific events are recognized as valid throughout the entire IPC pipeline, preventing them from being discarded during process-to-process communication.

### **2. Asynchronous Event Routing**
To bridge the process gap, we propose an asynchronous routing mechanism. When `AVPlayer` in the GPU discovers encrypted content, it will send a message via Mojo to the Renderer. 

We will wire this message directly into Chromium's existing `DemuxerManager`. By using this authorized entry point, the platform-specific GPU event is injected into the standard Chromium pathway, ensuring it reaches the web page safely without requiring a major rewrite of Chromium's internal interfaces.

### **3. A Dual-Format Starboard Layer**
The Starboard DRM layer should be updated to be "format-aware." We propose a branching strategy that allows the system to support both the future and the past:
*   **Modern Path:** If the system detects standard HLS content, it will treat the data as a raw URI and use a previously stored server certificate to generate the license request.
*   **Legacy Path:** For backward compatibility, the system will retain the ability to unpack YouTube's custom data blobs.

This same logic will apply to license responses, where the system will attempt to use raw binary data first (the modern standard) before falling back to Base64 decoding (the legacy approach).

### **4. Late-Binding DRM Attachment**
In a multi-process environment, we cannot guarantee that the DRM system will be ready at the exact moment the player needs it. We propose a "late-binding" strategy:
*   The system will securely queue any key requests in the GPU process during startup.
*   As soon as the DRM system is initialized—regardless of what state the player is in—the system will automatically "drain" these requests and start the handshake.

This ensures that the player doesn't time out just because the DRM setup took a few milliseconds longer than the video load.

## **Mojo IPC Extensions**
We propose adding the following signals to our specialized Mojo interfaces to support this flow:

| Message | Direction | Purpose |
| :--- | :--- | :--- |
| `OnEncryptedMediaInitDataEncountered` | GPU -> Renderer | Forwards the `skd://` URI from AVPlayer to JavaScript. |
| `UpdateServerCertificate` | Renderer -> GPU | Delivers the FairPlay certificate to the platform DRM system. |

---

# **Testing plan**

We will validate this design using a standard FairPlay test page. Success will be measured by the following milestones:
- The `encrypted` event fires in JavaScript with a valid `skd://` URI.
- The browser successfully generates a license request (SPC) using a certificate set via `setServerCertificate()`.
- The player accepts a raw binary license response and begins playback.
- Existing YouTube HLS content continues to play correctly using the legacy path.
