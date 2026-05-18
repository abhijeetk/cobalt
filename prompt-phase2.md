# PROMPT: Generate Design Document for FairPlay DRM & EME Integration (Phase 2)

## Role & Context
You are a Senior Software Architect specializing in media stacks and DRM systems. Your task is to write a **formal Design Document** for "Phase 2: FairPlay & EME Integration" for the Chrobalt project (a multi-process Cobalt/Chromium port).

**Critical Constraint:** This is a **proposal document**. The narrative should be an architectural analysis and a proposed solution based on research. Do NOT mention that internal experiments or prototype fixes have already been conducted. The goal is to provide a roadmap for the team to follow.

**Stylistic Requirement:** You must **strictly follow the structural and stylistic pattern** established in the "Phase 1: URL Player on Chrobalt" design document. This includes:
* Adopting the same sectional hierarchy (e.g., "One-page overview", "Summary", "Design Constraints").
* Maintaining the same professional, senior engineering tone.
* Using monochromatic, high-signal formatting with clear technical rationale.

## Documentation References (Deep Analysis Required)
To write this document, you must perform a **deep analysis** of the following sources. Do not simply summarize; you must "mine" these files for subtle technical gaps, threading assumptions, and data format mismatches that are not immediately obvious:
1. **C25 Legacy:** Refer to `/Users/abhijeet/code/cobalt-github/src/EME-C25-Flow.md` to understand the single-process, direct-callback architecture.
2. **Phase 1 Reference:** Refer to `/Users/abhijeet/Documents/Design Doc_ URL Player on Chrobalt.md` to match its structure and style perfectly. This is the foundational document for the URL Player project.
3. **Research & Gap Analysis (CRITICAL):** Deeply analyze the following workspace files:
    * `/Users/abhijeet/code/cobalt-github/src/FairPlay-GenerateRequest-Gap-Analysis.md`
    * `/Users/abhijeet/code/cobalt-github/src/ResearchReport.md`
    * `/Users/abhijeet/code/cobalt-github/src/EME-FairPlay-ConfigSelector-Analysis.md`
    * `/Users/abhijeet/code/cobalt-github/src/Plan-FairPlay-InitDataType-Support.md`

## Document Structure & Requirements

### 1. Executive Summary
Briefly explain the goal: Integrating the FairPlay Streaming (FPS) DRM stack into Chrobalt's multi-process architecture to support encrypted HLS playback via the URL Player.

### 2. Analysis of C25 Legacy Assumptions
You must identify and elaborate on the core assumptions that guided the C25 implementation but are incompatible with a modern multi-process browser. Beyond the basics, find and explain the "why" behind these issues:
* **Non-Standard Init Data:** C25 used a custom "fairplay" init data type that packed [skd_uri | content_id | certificate] into a single blob. Elaborate on how this deviates from the standard.
* **Implicit Certificate Flow:** C25 bypassed `setServerCertificate()`. Explain the architectural consequence of this in a multi-process environment where `generateRequest()` is asynchronous.
* **[REQUIRED: Find More]:** Analyze the provided `.md` files to identify at least 2-3 other assumptions (e.g., regarding threading, callback lifetimes, or memory management) that must be addressed.

### 3. The Chrobalt Challenge (Detailed Technical Gaps)
Detail the architectural gaps identified during research. Do not just list them; explain the technical failure mode for each:
* **Process Boundary Mismatch:** Explain how moving the `AVPlayer` to the GPU process "orphans" the encrypted events and why a standard `RendererClient` cannot handle them.
* **Initialization Data Support:** Chromium’s EME stack lacks native definitions for FairPlay-specific types. Explain why support for `SKD`, `SINF`, and `FAIRPLAY` is required for both modern and legacy compatibility.
* **Encoding & Format Mismatches:** Detail the specific failures caused by encoding differences (UTF-8 vs UTF-16LE) and data formats (Raw Binary vs Base64 CKC). Explain the "Mojo Timeout" behavior that occurs when these mismatches cause a silent failure.
* **[REQUIRED: Find More]:** Use the research files to find more subtle issues (e.g., IPC timing issues, CDM initialization races, or platform entitlement requirements) and elaborate on them here.

### 4. Proposed Architecture
Detail the step-by-step solution to bridge these gaps:
* **EME Enum Extension:** Propose extending `EmeInitDataType` in `eme_constants.h` to include `SINF`, `SKD`, and `FAIRPLAY`.
* **The "Matchmaker" Bridge:** Propose a strategy where `WebMediaPlayerImpl` acts as a matchmaker, wiring the `StarboardRendererClient` output to the `DemuxerManager` input to bridge the process gap.
* **Branching DRM Logic:** Propose a "Smart Decoder" in the Starboard layer that can distinguish between "skd" (standard) and "fairplay" (legacy) payloads and handle them idiomatically.
* **Certificate Persistence & Handshake Sync:** Propose a state-safe mechanism to store the server certificate and handle the race condition between license updates and player initialization.

### 5. Detailed Sequence Diagram
Include a Mermaid sequence diagram showing the end-to-end flow:
`AVPlayer (GPU) -> StarboardRenderer -> Mojo IPC -> StarboardRendererClient (Renderer) -> WebMediaPlayerImpl -> JavaScript (EME Event)`.

### 6. Success Criteria
* Successful resolution of the `generateRequest()` promise for standard FairPlay streams.
* Playback of encrypted HLS content using standard license servers.
* Maintaining backward compatibility with existing YouTube/C25 application logic.

---
**Style Note:** MONOSPACE and Monochromatic. Focus on high-signal technical rationale. Avoid filler.
