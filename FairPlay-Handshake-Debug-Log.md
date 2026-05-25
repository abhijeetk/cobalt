# FairPlay Handshake Debug Log

*Last updated: May 24, 2026*

This file records physical Apple TV runs for the AVSBDL FairPlay handshake. It is intended as the first place to check before retrying content identifier, SPC, CKC, or renderer-key changes.

## Current Status

The HLS demuxer and EME bridge are working:

- `.m3u8` is routed to Chromium `ManifestDemuxer`.
- `EXT-X-KEY` with `skd://302f80dd-411e-4886-bca5-bb1f8018a024:77FD1889AAF4143B085548B3C0F95B9A` is parsed.
- JS receives `encrypted` with `initDataType=skd`, 75 bytes.
- `com.youtube.fairplay.sbdl` is accepted.
- `MediaKeys` is created and server certificate is set.
- `session.generateRequest("skd", ...)` generates an SPC.
- Axinom returns a license response and `session.update()` reaches `DrmSystemFairplay::UpdateSession`.
- The May 24 12:42 diagnostic run found a bridge lifetime bug: the CKC pointer passed into `updateSession` was copied inside an async block, after the caller-owned buffer was no longer valid. This produced mostly `0xcd` bytes on-device even though replaying the captured SPC from the Mac returned plausible FairPlay CKC bytes.

The failing boundary is Apple accepting the CKC:

- `processContentKeyResponse:` is called.
- `AVContentKeySessionDelegate` then reports `CoreMediaErrorDomain Code=-42681`.
- `AVContentKeyRequest.contentKey` remains `nil`.
- Playback stalls at `time=0.0`.

## API-Based Decision

AVPlayer comparison is not currently available because that path is disabled. Based on Apple and Axinom APIs, the active AVSBDL shape should be:

- `requestIdentifier`: the manifest URI, `skd://302f80dd-411e-4886-bca5-bb1f8018a024:77FD1889AAF4143B085548B3C0F95B9A`.
- `contentIdentifier`: UTF-8 `302f80dd-411e-4886-bca5-bb1f8018a024:77FD1889AAF4143B085548B3C0F95B9A`, i.e. Axinom's Key URI value without the `skd://` scheme.
- `makeStreamingContentKeyRequestDataForApp` options: `{ AVContentKeyRequestProtocolVersionsKey: @[@1] }` for sample parity.
- License request auth: either `X-AxDRM-Message` header or `AxDrmMessage` query parameter is valid per Axinom's License Acquisition API, so the page's query-parameter usage is not a root-cause candidate by itself.

The full `skd://...` and raw UUID content identifiers are now ruled out for this implementation.

## Commands Used

Build:

```bash
COBALT_USE_INTERNAL_BUILD=1 autoninja -C out/tvos-arm64-device_debug nplb cobalt
```

Deploy/run:

```bash
./deploy_and_run_cobalt_device.sh \
  --bundle-id 'abhijeet.tvos.org.chromium.chrome.unittests.dev' \
  --device 'Abhijeet-TVOS' \
  --url 'https://people.igalia.com/akandalkar/fairplay/fairplay-urlplayer-test.html?v=2' \
  > /tmp/cobalt_eme_test.log 2>&1
```

Signing/provisioning warnings from `devicectl` and post-processing were ignored for these runs.

## Run Results

| Time | Log file | Content identifier passed to `makeStreamingContentKeyRequestDataForApp` | SPC | License | Result |
|:---|:---|:---|:---|:---|:---|
| 2026-05-24 01:02 IST | `/tmp/cobalt_eme_test.log` | `UUID:IV` string, 69 bytes | 8512 bytes | 1180 bytes, opaque prefix `714e3f04...` | `-42681`, `contentKey=nil`, stalled |
| 2026-05-24 01:07 IST | `/tmp/cobalt_eme_test_debug2.log` | `UUID:IV` string, 69 bytes | 6128 bytes, wrapped, messageId `89323a6ac5f6fcf7530490131aad5c8f` | 876 bytes, prefix `0000000000000000ffffffffffffffffcdcd...` | `-42681`, `contentKey=nil`, stalled |
| 2026-05-24 01:11 IST | `/tmp/cobalt_eme_test_raw_uuid.log` | Raw UUID bytes, 16 bytes: `302f80dd411e4886bca5bb1f8018a024` | 7888 bytes, wrapped, messageId `15d90dbe247d840aec018ecac07b37d8` | 1100 bytes, prefix `714e3f04...` | `-42681`, `contentKey=nil`, stalled |
| 2026-05-24 01:27 IST | `/tmp/cobalt_eme_test_full_skd_uri.log` | Full `skd://UUID:IV` URI, 75 bytes | 7744 bytes, wrapped, messageId `d39b32ee837f253d2d76ada628bf6379` | 1212 bytes, prefix `714e3f04...` | `-42681`, `contentKey=nil`, stalled |
| 2026-05-24 12:30 IST | `/tmp/cobalt_eme_test_axinom_key_uri_protocol_v1_retry.log` | Axinom key URI without scheme, `UUID:IV`, 69 bytes, protocol version 1 option | 6592 bytes, wrapped, messageId `58f68ea572f532914cfd6f17ac6c8325` | 1068 bytes, prefix `71cec6ff...` | `-42681`, `contentKey=nil`, stalled |
| 2026-05-24 12:42 IST | `/tmp/cobalt_eme_test_ckc_payload_scan.log` | Axinom key URI without scheme, diagnostic build | 7168 bytes, wrapped, messageId `bb3af26293959d5b862a94f4fba4b7d5` | 812 bytes in JS, but corrupted to mostly `0xcd` inside `UpdateSession` | `-42681`; root cause identified as async raw-pointer lifetime bug |
| 2026-05-24 12:47 IST | `/tmp/cobalt_eme_test_ckc_sync_copy.log` | Axinom key URI without scheme, sync CKC copy, stripped inner payload | 7552 bytes, wrapped, messageId `be10991a9208ea53d317699a1ae1d8a6` | 892 bytes, prefix `0000000100000000...`, stripped to 864-byte inner payload | `-42681`, `contentKey=nil`, stalled |
| 2026-05-24 12:51 IST | `/tmp/cobalt_eme_test_ckc_full_response.log` | Axinom key URI without scheme, sync CKC copy, full response, message ID patched | 8304 bytes, wrapped, messageId `07528dd919ecdd4e1bec2a0e1378f139` | 1020 bytes, prefix `0000000100000000...`, full response passed | `AVFoundationErrorDomain -11835`, "This content is not authorized"; message ID patch likely harmful |
| 2026-05-24 12:56 IST | `/tmp/cobalt_eme_test_ckc_passthrough_no_patch.log` | Axinom key URI without scheme, sync CKC copy, full response, no message ID patch | 7200 bytes, wrapped, messageId `119be684b37d1961cde2678a3ee52964` | 1164 bytes, prefix `0000000100000000...`, full response passed unchanged | No immediate `didFailWithError`; player still stalled because `Decrypt()` returned `kRetry` and samples were never written |

## What Worked

- Removing renderer `addContentKeyRecipient` calls avoided the earlier `-12161` cancellation path.
- The AVSBDL path now uses `DrmSystemFairplay`, not the ObjC `SBDApplicationDrmSystem` fallback.
- Passing `type="skd"` through to the ObjC FairPlay bridge works.
- Manual `processContentKeyRequestWithIdentifier:` fires `didProvideContentKeyRequest`.
- `makeStreamingContentKeyRequestDataForApp` succeeds for both tested content identifier shapes.
- The EME page receives a license-request message and posts it to Axinom.
- `session.update()` returns success at the Starboard CDM layer because the response is delivered to Apple.
- Replaying the exact captured SPC from the Mac returns a non-`0xcd` FairPlay-looking 812-byte response with prefix `0000000100000000...`, proving the Axinom response itself is not the `0xcd` data seen in `UpdateSession`.

## What Has Not Worked

- `UUID:IV` as the FairPlay content identifier does not produce an Apple-accepted CKC, even with `AVContentKeyRequestProtocolVersionsKey: @[@1]`.
- Raw 16-byte UUID as the FairPlay content identifier also does not produce an Apple-accepted CKC for this Axinom stream.
- Full `skd://UUID:IV` also does not produce an Apple-accepted CKC. This rules out the remaining content identifier shape found in commit `8278552a3b793`.
- Current license responses are not recognized as valid FairPlay CKC by Apple; `processContentKeyResponse:` fails with `-42681`.
- `AVSampleBufferAttachContentKey` is not reached with a valid key because `AVContentKeyRequest.contentKey` remains `nil`.
- Before the async-copy fix, `UpdateSession` copied the `key` pointer inside `_dispatchQueue`; the pointer was caller-owned and could already be invalid by the time the block ran.

## Current Evidence

Raw UUID run:

```text
Parsed SKD: contentIdentifierLen=16
contentIdentifierHex=302f80dd411e4886bca5bb1f8018a024
SPC generated: len=7888 wrapped=1
License received, size=1100 bytes
License wrapper probe: prefix=714e3f04 version=02000001
didFailWithError: CoreMediaErrorDomain code=-42681 contentKey=nil
```

`UUID:IV` run:

```text
Parsed SKD: contentIdentifierLen=69
contentIdentifierHex=33303266383064642d...4530463935423941
SPC generated: len=6128 wrapped=1
License received, size=876 bytes
License wrapper probe: prefix=00000000 version=00000000
didFailWithError: CoreMediaErrorDomain code=-42681 contentKey=nil
```

Full `skd://UUID:IV` run:

```text
Parsed SKD: contentIdentifierVariant=full_skd_uri contentIdentifierLen=75
SPC generated: len=7744 wrapped=1
License received, size=1212 bytes
License wrapper probe: prefix=714e3f04 version=02000001
didFailWithError: CoreMediaErrorDomain code=-42681 contentKey=nil
Status: time=0.0 paused=false readyState=4
EVENT: stalled
```

API-backed Axinom key URI plus protocol version 1 run:

```text
Parsed SKD: contentIdentifierVariant=axinom_key_uri_without_scheme contentIdentifierLen=69
SPC generated: len=6592 wrapped=1 messageId=58f68ea572f532914cfd6f17ac6c8325
License received, size=1068 bytes
License wrapper probe: prefix=71cec6ff version=01000001
didFailWithError: CoreMediaErrorDomain code=-42681 contentKey=nil
Status: time=0.0 paused=false readyState=4
EVENT: stalled
```

CKC payload scan run:

```text
SPC generated: len=7168 wrapped=1
License received, size=812 bytes
RawLicense fullBase64 len=812 value=AAAAAADKv8D//////zVAP83Nzc3...
Candidate payload scan: firstNonZero=5:ca,6:bf,7:c0,8:ff...
Mac replay with the same SPC and token returned HTTP 200, 812 bytes,
prefix=0000000100000000..., not the on-device 0xcd pattern.
Root cause: `updateSession` copied caller-owned `key` bytes after dispatch.
```

CKC sync-copy and pass-through runs:

```text
RawLicense fullBase64 prefix=AAAAAQAAAAA...
Axinom wrapper detected. Keeping full response for AVContentKeyResponse.
License wrapper message id comparison: response=... spc=... action=keep_server_response
FairPlay content key ready for session=...
```

Result: keeping the full server response and not patching the message ID stopped
the immediate `didFailWithError` path. The next blocker moved downstream: the
renderer never received samples while `Decrypt()` always returned `kRetry`.

Decrypt-gate run:

```text
Decrypt gate: session=... hasLicense=1 state=ready canPass=1 contentKey=present
Audio: GetContentKey returned found
Failed to attach content key.Error -11836(AVFoundationErrorDomain)
underlying_err: NSOSStatusErrorDomain Code=-12161
PIPELINE_ERROR_DECODE
```

Finding: handshake progressed far enough to produce an `AVContentKey`, but
`AVSampleBufferAttachContentKey` rejected it at the renderer boundary. The next
candidate is recipient lifecycle: register `AVSampleBufferAudioRenderer` and
`AVSampleBufferDisplayLayer` with `AVContentKeySession`.

Recipient registration run:

```text
AddContentKeyRecipient: <AVSampleBufferAudioRenderer ...>
AddContentKeyRecipient: <AVSampleBufferDisplayLayer ...>
Decrypt gate for session 1 state=Fulfilling licenseLen=1052 canPass=1 contentKey=present
Failed to attach content key.Error -11836(AVFoundationErrorDomain)
underlying_err: NSOSStatusErrorDomain Code=-12161
```

Result: recipient registration worked mechanically but did not clear the attach
failure.

Cryptor-before-attach run:

```text
Audio: CryptorSubsampleAuxiliaryData subsample_count=1 bytes=8
Decrypt gate for session 1 state=Fulfilling licenseLen=636 canPass=1 contentKey=present
Failed to attach content key.Error -11836(AVFoundationErrorDomain)
underlying_err: NSOSStatusErrorDomain Code=-12161
```

Result: adding the cryptor attachment before `AVSampleBufferAttachContentKey`
did not clear the attach failure. Next instrumentation logs the actual
clear/encrypted byte ranges and compares their sum to
`CMSampleBufferGetTotalSampleSize`, because the AAC builder strips an ADTS
header before creating the `CMSampleBuffer`.

Cryptor range debug run:

```text
Audio: CryptorSubsampleAuxiliaryData subsample_count=1 bytes=8
mapped_sample_bytes=342 cmsample_total_bytes=335
ranges= [0:clear=7,encrypted=335]
Failed to attach content key.Error -11836(AVFoundationErrorDomain)
underlying_err: NSOSStatusErrorDomain Code=-12161
```

Finding: the audio sample builder strips a 7-byte ADTS header before creating
the `CMSampleBuffer`, but the DRM subsample metadata still described those 7
clear bytes. This makes the cryptor attachment describe 342 bytes for a 335-byte
sample. A targeted fix now adjusts the first audio clear range by the stripped
prefix size before `AVSampleBufferAttachContentKey`.

Audio mapping fix run:

```text
Audio: adjusted cryptor mapping for stripped clear prefix removed=7
Audio: CryptorSubsampleAuxiliaryData subsample_count=1 bytes=8
mapped_sample_bytes=335 cmsample_total_bytes=335
ranges= [0:clear=0,encrypted=335]
Failed to attach content key.Error -11836(AVFoundationErrorDomain)
underlying_err: NSOSStatusErrorDomain Code=-12161
```

Result: the byte-range mismatch is fixed, but the attach failure remains. The
next Apple API-backed candidate is the format description suitability signal:
use `kCMAudioCodecType_AAC_LCProtected` plus
`kCMFormatDescriptionExtension_ProtectedContentOriginalFormat` for encrypted
AAC instead of the local `'qaac'` fourcc.

Protected AAC format run:

```text
Audio: adjusted cryptor mapping for stripped clear prefix removed=7
Audio: CryptorSubsampleAuxiliaryData subsample_count=1 bytes=8
mapped_sample_bytes=335 cmsample_total_bytes=335
ranges= [0:clear=0,encrypted=335]
Decrypt gate for session 1 state=Fulfilling licenseLen=1276 canPass=1 contentKey=present
Failed to attach content key.Error -11836(AVFoundationErrorDomain)
underlying_err: NSOSStatusErrorDomain Code=-12161
```

Result: `kCMAudioCodecType_AAC_LCProtected` plus
`kCMFormatDescriptionExtension_ProtectedContentOriginalFormat` did not clear
`AVSampleBufferAttachContentKey`. This makes the current boundary more specific:
the license is fulfilled and an `AVContentKey` object exists, but Apple still
rejects attaching it to the first protected audio sample.

## Next Debugging Candidates

1. Compare the `AVContentKey.contentKeySpecifier.identifier` and options
   against the sample key ID / SKD URI at attach time. Apple says the
   `AVContentKeySpecifier` must match suitability indications on the sample.
2. Inspect whether the first audio sample needs per-sample IV and encryption
   pattern metadata in addition to `CryptorSubsampleAuxiliaryData`; current code
   only attaches clear/protected byte ranges.
3. If audio attach succeeds but video later fails, run the same byte-range
   comparison for video and inspect video sample construction.
4. If byte ranges match but `AVSampleBufferAttachContentKey` still returns
   `-12161`, inspect whether additional per-sample IV / encryption pattern
   metadata is required for AVSBDL manual FairPlay attachment.
4. If Apple reports authorization failure again before sample writes, inspect
   whether the SPC request should be sent without EME wrapping.
5. Suppress duplicate `cenc` encrypted events for this FairPlay key system in
   the test page or EME bridge; they are not the direct `-42681` cause, but they
   add noise.

## Web Reference Findings

Sources checked on May 24, 2026:

- Apple `AVContentKeyRequest.makeStreamingContentKeyRequestData(forApp:contentIdentifier:options:)`
- Apple Technical Note TN2454, "Debugging FairPlay Streaming"
- Axinom DRM FairPlay documentation
- Axinom `drm-fairplay-integration-sample`

Concrete findings:

1. Apple describes `appIdentifier` as the app/provider certificate data and `contentIdentifier` as opaque content ID bytes. If `AVContentKeyRequestProtocolVersionsKey` is not provided, Apple defaults to protocol version `1`.
2. Axinom's FairPlay sample extracts `keyRequest.identifier`, removes `skd://`, converts the remaining string to UTF-8, and passes that as `contentIdentifier`.
3. Axinom's sample explicitly calls `makeStreamingContentKeyRequestData(..., options: [AVContentKeyRequestProtocolVersionsKey: [1]])`.
4. Axinom's sample sends the SPC as the POST body and sends the license token in the HTTP header `X-AxDRM-Message`.
5. Our current HTML test page sends the token as `?AxDrmMessage=...` query parameter. This differs from Axinom's native AVFoundation sample, but it is not enough to call root cause because the same HTML page reportedly worked with AVPlayer.
6. Raw 16-byte UUID is not supported by the Axinom sample pattern for content ID. The reference-backed candidate is the UTF-8 string after `skd://`, including the `:IV` suffix when present in the manifest URI.
7. The in-repo AVPlayer path currently disables the raw `skd` encrypted event branch with `if (FALSE && ...)`, so the reported working path probably used `initDataType="fairplay"` and the page's packed `[skd URL, contentId, certificate]` branch.

Additional instrumentation added after the full-SKD run:

- `SBDApplicationDrmSystem` now logs AVPlayer-path content identifier bytes, SPC prefix, CKC prefix, and `contentKey` after `processContentKeyResponse:`.
- If AVPlayer is re-enabled later, these logs can capture the working path exactly instead of continuing content-ID guessing.
- `DrmSystemFairplay` now logs full base64 SPC/license values and candidate license payload offsets before any CKC transform. This is diagnostic-only and is intended to determine whether the `71cec6ff...` / `714e3f04...` response is a wrapped CKC or already the CKC bytes Apple expects.
