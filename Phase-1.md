# **Design Doc: URL Player on Chrobalt**

*Authors: [abhijeet@igalia.com](mailto:abhijeet@igalia.com)*  
*May 2026*

# **One-page overview**

### **Summary**
This document describes how we can delegate HLS playback to the native `AVPlayer` on tvOS, for high performance and low power consumption.
We divide this work into two phases:
- Phase 1: Enable HLS playback on tvOS (clear, non-protected content)
- Phase 2: Enable protected HLS playback on tvOS (FairPlay DRM)

This document covers Phase 1.

### **Platforms**
tvOS

### **Team**
Cobalt Media Team

### **Bug**
[b/512045535](https://partnerissuetracker.corp.google.com/u/1/issues/512045535) (Reference)

### **Code affected**
`media/starboard`, `media/filters`, `media/mojo`, `third_party/blink/renderer/platform/media`

---

# **Design**

## **Problem Statement**
On tvOS, the most performant and reliable way to play HLS content is via the native `AVPlayer`. However, Chromium’s media pipeline is designed to be a "Stream-Based" engine where the Renderer process demuxes bytes and sends raw packets to the GPU process. For HLS, this is suboptimal because it bypasses Apple's internal optimizations and FairPlay DRM integration.

To support `AVPlayer`, we must solve a **process boundary problem**:
1. The **Renderer process** knows the URL from the HTML `<video>` element but lacks hardware access.
2. The **GPU process** has hardware access but is blind to the web-page context.
3. The **Mojo IPC layer** must bridge this gap by carrying the URL string across the boundary.

## **Proposed Architecture**

### **Process Boundary Diagram**

![](https://notes.igalia.com/uploads/b60a727e-fe08-480f-8598-ed943efc34df.png)

```
https://mermaid.live/edit#pako:eNp9U-9vmzAQ_Vcsf2kqkSxNQn6grdIC01op6VBoGmljHwy-JWyAkTFV0zT_-844rEPpxgfA5_funt-dDzQWHKhDt5IVO3I_D3OCT1lFJhDSFeQcJEjiSxFDWYbUQPRzc79cfAup_pDHhIMgpYw_XJRKAst62bCaXoT0-yt-s_QRvoFoCTxhfsr2IG-zIm2B1r6HoLVMzb4HWfUEsgUJVi5CAsVkJJjkjUQ3TSBX7yP57rqzFD8FMYHLFretnnS711pWS6MOvoTUEwvBOIn2BStLKInHFAtEJWMI6YuW2dJ8IrlMygTB69VCo1CpQaHEMD8z97O_fsvXYLV563wbZBVnVryFbEP8ucZExs-5TPgWWoCPD7ot-K4Bxr87ppJHIDeL4F_2ocjavWD1d8yE_HmrfB3E_GdWoD0n33S_HBKAMg5j-zuVTC-Nh5sGjzlIt1cTMlCMY0dIh1cSxYrcamYweYYTsanSkEyVL7l3org7lm_BwsiDpuKhcSMNMIHZ-dPCs_KQx3JfKOAkFrnCKSMcFMQY-H_lTw2vvgK3eaL0WGFUVJhGNnSXWnglE04dJSuw8LQyY3pJDzpzSNUOMpTn4C9n8peeniNyCpZ_FSJraFJU2x11frC0xFVVoGHgJQzH7xVSj4yry1PHrjNQ50CfqDMY93uDUX88sgf2dDa0pyOL7qkzmvRGs9lsYtvD_tXYvpqNjhZ9rov2e9OJffwNNuxP5A

graph TB
    subgraph "Renderer Process"
        HTML["HTML video src='stream.m3u8'"]
        WMP["WebMediaPlayerImpl"]
        UPD["UrlPlayerDemuxer"]
        SRC["StarboardRendererClient<br/>(Mojo Client)"]
        
        HTML --> WMP
        WMP -->|"DoLoad bypasses DataSource"| UPD
        UPD -->|"Carries URL"| SRC
    end

    subgraph "GPU Process"
        SRW["StarboardRendererWrapper"]
        SR["StarboardRenderer"]
        SPB["SbPlayerBridge"]
        AVP["AVPlayer<br/>(Native HLS)"]
        
        SRW --> SR
        SR --> SPB
        SPB --> AVP
    end

    SRC -->|"Mojo: SetSourceUrl(url)"| SRW

    AVP -.->|"metadata (duration, video size)"| SR
    SR -.->|"Mojo: OnDurationChange, OnVideoNaturalSizeChange"| SRC

    AVP -.->|"encrypted content detected"| SR
    SR -.->|"Mojo: OnEncryptedMediaInitDataEncountered"| SRC
    
```

## **HLS Detection Strategy**

### **Background: How C25 did it**

In Cobalt 25 (C25), HLS detection was straightforward. A single helper function `ResourceNeedsUrlPlayer()` in `cobalt/dom/html_media_element.cc` checked if the URL contained `"hls_variant"`, a YouTube-specific query parameter that indicates the resource is an HLS stream. If the check passed, C25 routed playback to `SbUrlPlayerCreate` instead of the standard stream-based player. This worked well because C25 had a single-process architecture where the DOM layer had direct access to both the URL and the player creation API. [[source]](https://github.com/youtube/cobalt/blob/25.lts.1%2B/cobalt/dom/html_media_element.cc#L98-L103)

### **Why Chrobalt needs a different approach**

In Chrobalt, we cannot simply check the URL in one place and call the player in the same place. The Renderer process sees the URL (from the `<video>` element), but the GPU process is the one that actually creates `AVPlayer`. Between them sits the Chromium media pipeline, which assumes every media resource goes through a `DataSource > Demuxer > Renderer` chain. If we don't intercept this flow at the right points, two things go wrong:

- **Chromium tries to download the media URL** via `MultiBufferDataSource`. This is unnecessary because AVPlayer expects a URL and handles all fetching internally (manifest parsing, variant selection, segment downloading, buffering). We just need to pass the URL through to AVPlayer without Chromium trying to download it.
- **Chromium's built-in HLS demuxer (`ManifestDemuxer`) may claim the URL** if the `kBuiltInHlsPlayer` feature flag is enabled, bypassing our `AVPlayer` path entirely.

So we would need to intercept at two points: once to skip the data source, and once to select the right demuxer.

### **What we can check for**

We propose checking for two markers in the URL string, either of which would indicate an HLS resource:

| Marker | What it means |
| :--- | :--- |
| `hls_variant` | YouTube query parameter that flags HLS variant playlists. Based on C25 behavior, this should be present in production YouTube streaming. |
| `.m3u8` | Standard HLS manifest file extension. A practical fallback for testing with third-party HLS streams. |

### **Detection points (two layers)**

We propose intercepting the pipeline at two places. Both would perform the same URL check, but they serve different purposes:

**1. `WebMediaPlayerImpl::DoLoad()`** (`third_party/blink/renderer/platform/media/web_media_player_impl.cc`)

This is the first point of contact. When `DoLoad()` is called with a media URL, it normally creates a `MultiBufferDataSource` to fetch the resource over the network. For HLS URLs, we would skip that entirely and go straight to `StartPipeline()`.

**2. `DemuxerManager::CreateDemuxer()`** (`media/filters/demuxer_manager.cc`)

This is where the pipeline decides which demuxer to use. We propose placing our check *before* the `ENABLE_HLS_DEMUXER` block so that the Starboard URL player path takes priority over Chromium's built-in `ManifestDemuxer`, even when the `kBuiltInHlsPlayer` feature flag is enabled. If the URL matches, we would create a lightweight stub demuxer (described in the Core Components section below) that simply carries the URL forward. No manifest parsing, no segment fetching.

### **Why the check is duplicated**

Having the same URL check in two places may look redundant, but each serves a distinct purpose. `DoLoad()` prevents the data source from being created and downloaded (Blink-layer), while `CreateDemuxer()` ensures the correct demuxer is selected (a media pipeline layer). These are independent code paths and removing either one would break the flow.

## **Core Components**

Below are the key components we propose to introduce or modify. A prototype exists for feasibility testing, but the exact shape of these components may change as we go.

### **1. UrlPlayerDemuxer (Renderer)** `[proposed, new]`

We propose a lightweight stub demuxer whose main job is to carry the HLS URL through the pipeline. It would not fetch segments or parse manifests. At a minimum, it needs to:
*   Expose the HLS URL so that downstream components can retrieve it (e.g. via a `GetMediaUrl()` method).
*   Provide dummy audio/video streams to satisfy the Chromium Pipeline’s validation checks. These streams would not deliver real data.

The exact interface may grow as we discover additional pipeline requirements.

### **2. StarboardRendererClient (Renderer)** `[proposed modification]`

The existing Mojo-side proxy that talks to the GPU process. We propose modifying it to extract the URL from the `UrlPlayerDemuxer` and send a `SetSourceUrl(url)` message via Mojo. This would ensure the GPU side knows the URL before it tries to create the player. The same Mojo pipe could also carry messages in the other direction (GPU to Renderer), such as duration updates and encrypted media events from AVPlayer.

### **3. StarboardRenderer (GPU)** `[proposed modification]`

In C25, `SbPlayerPipeline` handled this by having two separate methods: `CreateUrlPlayer()` for URL-based playback and `CreatePlayer()` for the standard stream-based path. It branched early based on an `is_url_based_` flag.

In Chrobalt, the equivalent component is `StarboardRenderer`. We propose taking a similar approach: when a source URL has been set, the renderer would call `SbUrlPlayerCreate` instead of `SbPlayerCreate` and skip the buffer-feeding logic that does not apply when AVPlayer manages its own fetching. If the branching gets too complex, we may extract it into a dedicated class later.

## **Proposed Mojo IPC Messages**

The URL player would reuse most of the existing Mojo interfaces (`mojom::Renderer`, `mojom::RendererClient`) that the standard Starboard player already uses. Below are only the new messages we propose adding to support the URL player path. These would be defined in `renderer_extensions.mojom`. Additional messages may be needed as the implementation progresses.

### **Renderer to GPU**
| Message | Interface | Purpose |
| :--- | :--- | :--- |
| `SetSourceUrl(url)` | `StarboardRendererExtension` | Would pass the HLS URL to the GPU process. Sent before `Initialize` so the URL is available when the player is created. |

### **GPU to Renderer**
| Message | Interface | Purpose |
| :--- | :--- | :--- |
| `OnDurationChange(d)` | `StarboardRendererClientExtension` | Would report stream duration discovered by AVPlayer after loading the HLS manifest. In the normal path, the demuxer reports this from the Renderer side, but for URL player it would come from the GPU side. |
| `OnEncryptedMediaInitDataEncountered(type, data)` | `StarboardRendererClientExtension` | Would forward FairPlay init data from AVPlayer to the Renderer, which fires the `'encrypted'` event on the `<video>` element so JavaScript can begin the EME key exchange. Needed for Phase 2. |

## **Platform Guard Strategy**

The URL player path is specifically developed for tvOS. The code it touches spans two areas: upstream Chromium code and Cobalt's Starboard media layer. We use a different guard in each area, depending on what build flags are available there.

| Where | Guard | Why |
| :--- | :--- | :--- |
| **Non-Starboard code** (upstream Chromium, Cobalt content layer fork in `//cobalt`, etc.) | `BUILDFLAG(IS_IOS_TVOS) && BUILDFLAG(USE_STARBOARD_MEDIA)` | `USE_STARBOARD_MEDIA` alone is not enough because it is true for some Cobalt variants, not just tvOS. Adding `IS_IOS_TVOS` limits it to the right platform. |
| **Starboard code** (`media/starboard/`, etc.) | `#if SB_HAS(PLAYER_WITH_URL)` | This flag would be defined only by the tvOS platform (in `starboard/tvos/shared/configuration_public.h`) and directly expresses the platform capability we need. |
| **Mojo IPC definitions** (`media/mojo/`) | Always present | Mojom's `EnableIf` cannot target tvOS specifically, so these methods are always declared. They are harmless if never called, and the upstream guard ensures they are never called on non-tvOS builds. |

The first guard prevents the URL player path from being triggered. The second guard prevents the URL player implementation from being compiled. We need both because `BUILDFLAG(IS_IOS_TVOS)` is available across the whole codebase, while `SB_HAS(PLAYER_WITH_URL)` is only available in Starboard headers.

## **Sequence Diagrams**

Detailed sequence diagrams for the HLS playback flow will be added once the implementation is further along. The Process Boundary Diagram above shows the high-level component interactions and data flow that these sequences would follow.

---

# **Testing plan**

For early validation of Phase 1, we put together a [test page](https://people.igalia.com/akandalkar/hls-urlplayer-test.html) that loads a non-protected HLS stream (`.m3u8` from `test-streams.mux.dev`) using a plain `<video src>` element with no MSE involvement. It displays playback events (`loadstart`, `loadedmetadata`, `playing`, `error`) on screen to confirm the URL player path is working end-to-end.

**Manual verification on tvOS device:**
- Audio and video play back correctly.
- Native logs confirm `SbUrlPlayerCreate` is called instead of `SbPlayerCreate`.
- The test page shows `playing` status, indicating the full pipeline completed successfully.
- Standard MP4/WebM playback (non-HLS) remains unaffected by the platform guards.

We are happy to adapt our test setup to any test pages or HLS streams the team recommends as we move towards integration with the YouTube app.
