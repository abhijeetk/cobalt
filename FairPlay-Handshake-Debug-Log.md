# FairPlay Handshake Debug Log

*Last updated: May 25, 2026*

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

Key specifier debug run:

```text
Audio: AttachContentKey suitability
sample_key_id=302f80dd411e4886bca5bb1f8018a024
sample_iv=77fd1889aaf4143b085548b3c0f95b9a iv_size=16
scheme=1 crypt_block=0 skip_block=0
content_key=keySystem=FairPlayStreaming
identifier=skd://302f80dd-411e-4886-bca5-bb1f8018a024:77FD1889AAF4143B085548B3C0F95B9A
options={ ProtocolVersionsKey = (1); }
Failed to attach content key.Error -11836(AVFoundationErrorDomain)
underlying_err: NSOSStatusErrorDomain Code=-12161
```

Finding: the first concrete suitability mismatch is that the sample key ID is
the 16-byte UUID, while `AVContentKeySpecifier.identifier` is the full SKD URI.
The next test kept the SPC `contentIdentifier` as Axinom's `UUID:IV`, but used
the binary 16-byte key ID as the `AVContentKeySession` request identifier so the
fulfilled key's specifier could match `SbDrmSampleInfo.identifier`.

Key-ID request identifier test:

```text
Parsed SKD: requestIdentifier='{length = 16, bytes = 0x302f80dd411e4886bca5bb1f8018a024}'
Calling processContentKeyRequestWithIdentifier:
  identifier='{length = 16, bytes = 0x302f80dd411e4886bca5bb1f8018a024}'
Decrypt gate for session 1 state=WaitingForSPC licenseLen=0 canPass=0 contentKey=nil
```

Result: this regressed the handshake. AVFoundation did not call
`didProvideContentKeyRequest`, no SPC was generated, and no license request was
sent. The failed identifier experiment was reverted. Current conclusion: keep
the full SKD URI as the request identifier because that path produces SPC, CKC,
and an `AVContentKey`; investigate the sample-side suitability mismatch next.

Reverted identifier + sample metadata run:

```text
didProvideContentKeyRequest:
  identifier='skd://302f80dd-411e-4886-bca5-bb1f8018a024:77FD1889AAF4143B085548B3C0F95B9A'
SPC generated: len=7600 wrapped=1
UpdateSession: keySize=1148
Decrypt gate for session 1 state=Fulfilling licenseLen=1148 canPass=1 contentKey=present

Audio: Sample suitability metadata
  format_subtype=paac(0x70616163)
  format_extensions={
      CommonEncryptionOriginalFormat = 1633772320;
  }
  sample_attachment={
      CryptorSubsampleAuxiliaryData = {length = 8, bytes = 0x000000004f010000};
  }

Audio: AttachContentKey suitability
  sample_key_id=302f80dd411e4886bca5bb1f8018a024
  sample_iv=77fd1889aaf4143b085548b3c0f95b9a iv_size=16
  content_key.identifier=skd://302f80dd-411e-4886-bca5-bb1f8018a024:77FD1889AAF4143B085548B3C0F95B9A
Failed to attach content key.Error -11836(AVFoundationErrorDomain)
underlying_err: NSOSStatusErrorDomain Code=-12161
```

Finding: the handshake is no longer blocked by linker errors or SPC/CKC
generation. The current failure is at the CoreMedia suitability boundary. The
manually-built audio `CMSampleBuffer` only carries:

- protected AAC subtype (`paac`)
- `CommonEncryptionOriginalFormat = mp4a`
- `CryptorSubsampleAuxiliaryData`

It does not carry the full SKD URI, key URI, or an ISO sample-entry protection
atom such as `sinf`/`schm`/`tenc`. That explains why the same page can work via
AVPlayer: AVPlayer owns the HLS asset pipeline and has the manifest
`EXT-X-KEY`/SKD context while our AVSBDL path reconstructs elementary
`CMSampleBuffer`s with only partial protection metadata.

Audio-drop video probe:

For one diagnostic run only, audio attach failure was logged and the first audio
sample was dropped so video could reach `AVSampleBufferAttachContentKey`. This
temporary behavior was reverted after the probe.

```text
Audio: diagnostic_drop_after_attach_failure allowing video renderer to reach
AVSampleBufferAttachContentKey

Video: CryptorSubsampleAuxiliaryData
  mapped_sample_bytes=1242 cmsample_total_bytes=1202
  ranges=[0:clear=823,encrypted=419]
Video: Sample suitability metadata
  format_subtype=avc1(0x61766331)
  SampleDescriptionExtensionAtoms={ avcC = ... }
Video: AttachContentKey result=0
Failed to attach content key.Error -11836(AVFoundationErrorDomain)
underlying_err: NSOSStatusErrorDomain Code=-12161
```

Finding: video also had a stripped-prefix mapping mismatch. The key-frame
builder skipped 40 bytes before creating the `CMSampleBuffer`, but the DRM
range still described the original 1242-byte input buffer. A video-side mapping
adjustment now subtracts the stripped clear prefix, mirroring the earlier audio
ADTS fix.

Video mapping fix probe:

```text
Video: adjusted cryptor mapping for stripped clear prefix removed=40
Video: CryptorSubsampleAuxiliaryData
  mapped_sample_bytes=1202 cmsample_total_bytes=1202
  ranges=[0:clear=783,encrypted=419]
Video: AttachContentKey result=0
Failed to attach content key.Error -11836(AVFoundationErrorDomain)
underlying_err: NSOSStatusErrorDomain Code=-12161
```

Result: the video byte-range mismatch is fixed, but video still fails the same
attach suitability check. Combined with the audio result, `-12161` is not
explained by cryptor range size mismatch alone. The remaining shared gap is
that both manually-built samples lack FairPlay/SKD suitability metadata tying
the sample to the `AVContentKeySpecifier.identifier`.

Final current-state run after reverting the temporary audio drop:

```text
didProvideContentKeyRequest: identifier='skd://302f80dd-411e-4886-bca5-bb1f8018a024:77FD1889AAF4143B085548B3C0F95B9A'
SPC generated: len=8048 wrapped=1
UpdateSession: keySize=1020
Decrypt gate for session 1 state=Fulfilling licenseLen=1020 canPass=1 contentKey=present
Audio: CryptorSubsampleAuxiliaryData
  mapped_sample_bytes=335 cmsample_total_bytes=335
  ranges=[0:clear=0,encrypted=335]
Audio: Sample suitability metadata
  format_subtype=paac(0x70616163)
  format_extensions={ CommonEncryptionOriginalFormat = 1633772320; }
Audio: AttachContentKey suitability
  content_key.identifier=skd://302f80dd-411e-4886-bca5-bb1f8018a024:77FD1889AAF4143B085548B3C0F95B9A
Failed to attach content key.Error -11836(AVFoundationErrorDomain)
underlying_err: NSOSStatusErrorDomain Code=-12161
```

This is the latest non-temporary behavior in the deployed build.

## Metadata Parity Investigation - May 25, 2026

Research inputs checked:

- Apple tvOS 26.4 SDK headers:
  - `AVContentKeySession.h` says `AVSampleBufferAttachContentKey` expects an
    `AVContentKeySpecifier` matching content-key-system suitability indications
    available to the client.
  - `AVContentKeyRequest.identifier` notes that HLS `AVURLAsset` identifiers
    must be `NSURL` values matching media-playlist key URIs.
  - `CMSampleBuffer.h` documents
    `kCMSampleAttachmentKey_CryptorSubsampleAuxiliaryData` as only clear/protected
    byte-range pairs from `senc`; no public tvOS key was found for a separate
    per-sample IV or pattern attachment.
  - `CMFormatDescription.h` exposes
    `kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms` and
    `kCMFormatDescriptionExtension_ProtectedContentOriginalFormat`.
- WebKit `SourceBufferParserAVFObjC.mm` uses `AVStreamDataParser` via
  Apple SPI headers (`AVStreamDataParserSPI.h`) and has a
  `didProvideContentKeySpecifier` path that forwards
  `keySpecifier.initializationData`. This supports using WebKit as a reference,
  but not as direct Cobalt app code because the parser API is not public in the
  tvOS SDK headers.
- Local `FairPlayStreamingOverview.pdf` confirms FPS decrypts video per frame
  and audio per sample, and that key handling/decryption occur in the kernel.
  It does not document manual `CMSampleBuffer` construction details.
- Local `HLS-Player-Design-Doc.pdf` contains no useful `sinf/tenc` or
  `AVStreamDataParser` details. `Cobalt-Apple-TV-Media-Pipeline.pdf` only
  confirms FairPlay/Apple TV pipeline context.

Ranked options after research:

1. **Use `AVStreamDataParser` as a reference, not implementation.** WebKit's
   Apple MSE path gets parsed `CMSampleBuffer`s and key specifier metadata from
   AVFoundation together. This is likely why metadata parity works there, but
   `AVStreamDataParser` is SPI in this SDK.
2. **Enrich `CMFormatDescription` with sample description atoms.** This is the
   only public path found for protected sample-description metadata. It can carry
   atom payloads such as `sinf`, whose children can describe `frma`, `schm`, and
   `schi/tenc`.
3. **Align key specifier identity with HLS key URI.** Apple headers explicitly
   say HLS `AVContentKeyRequest.identifier` should be an `NSURL` matching a
   playlist key URI. Our current request identifier is an `NSString` containing
   the same `skd://...` text.
4. **Per-sample IV/pattern attachments are not currently supported by public
   evidence.** The only public sample attachment found is byte-range data. If
   IV/pattern metadata is required separately, the current public API surface
   does not reveal a direct key.
5. **Native harness remains the fallback.** If public metadata probes fail, a
   minimal native parser/harness should capture `CMFormatDescription` extensions
   and sample attachments from an AVFoundation-produced protected sample.

Audio `sinf` probe 1:

```text
Build: COBALT_USE_INTERNAL_BUILD=1 autoninja -C out/tvos-arm64-device_debug nplb cobalt
Log: /tmp/cobalt_sinf_audio_probe.log
Audio: adding FairPlay sinf key_id=302f80dd411e4886bca5bb1f8018a024
  iv_size=16 crypt_block=0 skip_block=0 sinf_payload_size=72
Audio: Sample suitability metadata
  format_subtype=paac(0x70616163)
  CommonEncryptionOriginalFormat = 1836069985
  SampleDescriptionExtensionAtoms = { sinf = length 72 }
Result: AVSampleBufferAttachContentKey still fails:
  AVFoundationErrorDomain -11836 / NSOSStatusErrorDomain -12161
```

This proved the `sinf` atom dictionary reaches CoreMedia, but it accidentally
changed the CM original audio format marker from the previous `aac ` value to
`mp4a`, and described a per-sample IV without any public IV attachment.

Audio `sinf` probe 2:

```text
Build: COBALT_USE_INTERNAL_BUILD=1 autoninja -C out/tvos-arm64-device_debug nplb cobalt
Log: /tmp/cobalt_sinf_audio_constant_iv.log
Audio: adding FairPlay sinf key_id=302f80dd411e4886bca5bb1f8018a024
  iv_size=16 crypt_block=0 skip_block=0 sinf_payload_size=89
Audio: Sample suitability metadata
  format_subtype=paac(0x70616163)
  CommonEncryptionOriginalFormat = 1633772320
  SampleDescriptionExtensionAtoms = { sinf = length 89 }
Result: AVSampleBufferAttachContentKey still fails:
  AVFoundationErrorDomain -11836 / NSOSStatusErrorDomain -12161
```

This restored the CM original format marker and encoded the sample IV as a
`tenc` constant IV. It still did not satisfy the suitability check. Current
interpretation: format-description atom enrichment alone is not enough while
the obtained `AVContentKeySpecifier.identifier` remains a manually-triggered
`NSString` SKD URI. The next smallest probe is to request the key with an
`NSURL` identifier matching the playlist key URI, as Apple documents for HLS
content key requests.

SKD `NSURL` identifier probe:

```text
Log: /tmp/cobalt_skd_url_identifier.log
Calling processContentKeyRequestWithIdentifier:
  identifier='skd://302f80dd-411e-4886-bca5-bb1f8018a024:77FD1889AAF4143B085548B3C0F95B9A'
  identifierClass=__NSCFString
```

`NSURL URLWithString` did not produce an `NSURL` for this Axinom `skd://UUID:IV`
form, likely because the suffix after the UUID is parsed as a nonnumeric port.
The probe therefore fell back to the same `NSString` identifier and reproduced
the existing `-12161` attach failure.

SKD `initializationData` probe:

```text
Log: /tmp/cobalt_skd_initdata.log
Calling processContentKeyRequestWithIdentifier ... initDataLen=75
No didProvideContentKeyRequest fired.
Decrypt gate for session 1 state=WaitingForSPC licenseLen=0 canPass=0 contentKey=nil
```

Passing the SKD URI bytes as `initializationData` regressed key request
creation. That behavior was reverted; the working key path continues to pass
`initializationData:nil`.

## WebKit Standalone HLS Fixture - May 25, 2026

Reference setup:

- Local resource: `fairplay-fps-hls-test-resource/`
- Setup guide: `WebKit-FairPlay-LayoutTest-Setup-Guide.md`
- Downloaded comparison page:
  `fairplay-reference-pages/fairplay-urlplayer-test.html`
- Source URL:
  `https://people.igalia.com/akandalkar/fairplay/fairplay-urlplayer-test.html`

The Axinom comparison page uses MP4 `cbcs` EME capability probes while the
video element plays HLS. The local WebKit fixture now follows that Cobalt-
compatible shape:

```text
initDataTypes: fairplay, skd, sinf, cenc
videoCapabilities: video/mp4; codecs="avc1.4D401F", encryptionScheme=cbcs
audioCapabilities: audio/mp4; codecs="mp4a.40.2", encryptionScheme=cbcs
```

The page fetches `content/prog_index.m3u8` and derives codecs from
`EXT-X-STREAM-INF CODECS` if present. This fixture's playlist has no `CODECS`
attribute, so the fallback codecs were verified from the referenced
`content/main.ts`:

```text
H.264 Main L3.1 -> avc1.4D401F
AAC-LC -> mp4a.40.2
```

Run:

```bash
./deploy_and_run_cobalt_device.sh \
  --bundle-id 'abhijeet.tvos.org.chromium.chrome.unittests.dev' \
  --device 'Abhijeet-TVOS' \
  --url 'http://192.168.1.3:8000/media/fairplay/fps-hls.html' \
  -- --unsafely-treat-insecure-origin-as-secure=http://192.168.1.3:8000 \
  2>&1 | tee /tmp/cobalt_fps_hls_playlist_derived_codecs_20260525_105016.log
```

Device log:

```text
/tmp/cobalt_device_debug_20260525_105101.log
```

What worked:

```text
FETCH: playlist received status=200 ok=true
INFO: playlist has no CODECS attribute; using verified fixture codecs from content/main.ts video=avc1.4D401F audio=mp4a.40.2
CHECK: typeof navigator.requestMediaKeySystemAccess=function
PROMISE: requestMediaKeySystemAccess resolved keySystem=com.youtube.fairplay.sbdl
PROMISE: createMediaKeys resolved
PROMISE: setServerCertificate resolved
CALL: session.generateRequest initDataType=skd bytes=12
Calling processContentKeyRequestWithIdentifier: identifier='skd://twelve' identifierClass=NSURL certLen=1235
didProvideContentKeyRequest: identifier='skd://twelve' identifierClass=NSURL
SPC generated: len=7168 wrapped=1
Fulfilling FairPlay request for session 1
Receiving session updated notification ... status: success
PROMISE: session.update() resolved
PROMISE: video.setMediaKeys resolved
CDM set successfully.
```

What did not work:

```text
AudioDecoderConfig ... encryption scheme: Unencrypted
VideoDecoderConfig ... encryption scheme: Unencrypted
Video WriteSample: drm_system=set drm_info=null has_drm=0
AVSBAR status failed ... AVErrorFourCharCode='fmt?'
PIPELINE_ERROR_DECODE
```

Current interpretation:

- The local WebKit fixture handshake is now successful through EME, SPC, CKC,
  `session.update()`, and `video.setMediaKeys()`.
- The secure-origin override is required for the HTTP fixture because Blink
  exposes `navigator.requestMediaKeySystemAccess` only in a secure context.
- The remaining blocker is sample metadata propagation. Cobalt's HLS path emits
  the SKD init data from `EXT-X-KEY`, but the MP2T parser still initializes both
  audio and video configs as unencrypted and emits samples without
  `DecryptConfig`.

Existing SAMPLE-AES support checked:

```text
media/formats/mp2t/mp2t_stream_parser.cc
media/formats/mp2t/es_parser_adts.cc
media/formats/mp2t/es_parser_h264.cc
```

The parser can already attach `DecryptConfig` when it receives encryption
scheme/key/IV metadata. The current WebKit fixture's TS file contains PAT/PMT
and SAMPLE-AES private descriptors, but does not contain the CAT/CETS ECM
metadata path that Chromium's mp2t parser currently uses to call
`RegisterEncryptionScheme` and `RegisterNewKeyIdAndIv`.

Next Cobalt code direction:

1. Add a narrow HLS-to-MP2T metadata bridge so `EXT-X-KEY` SAMPLE-AES metadata
   reaches `Mp2tStreamParser` before protected TS data is parsed.
2. Keep the existing parser's ADTS/H264 SAMPLE-AES subsample logic; do not add a
   second TS parser.
3. For playlists without explicit `IV`, derive the segment IV from media
   sequence only where HLS SAMPLE-AES rules apply, and log the derived value.
4. Once `DecoderBuffer::decrypt_config()` appears, rerun the same deploy/log
   cycle and check whether `Video WriteSample` changes from `drm_info=null` to
   `drm_info=present`.

## HLS SAMPLE-AES Bridge Review - May 25, 2026

Commit reviewed:

```text
aa909f5e309c1 [Encrypted-HLS] Bridge HLS EXT-X-KEY SAMPLE-AES metadata into TS parser
```

Build verification:

```bash
COBALT_USE_INTERNAL_BUILD=1 autoninja -C out/tvos-arm64-device_debug nplb cobalt
```

Result:

```text
[27/27] POST PROCESSING //cobalt:cobalt(//build/toolchain/ios:ios_clang_arm64)
exit code 0
```

Semgrep verification:

```text
Semgrep 1.151.0 scanned:
- media/filters/hls_rendition_impl.cc
- media/filters/chunk_demuxer.cc
- media/formats/mp2t/mp2t_stream_parser.cc
results: []
```

Clean deploy/run verification:

```bash
./deploy_and_run_cobalt_device.sh \
  --bundle-id 'abhijeet.tvos.org.chromium.chrome.unittests.dev' \
  --device 'Abhijeet-TVOS' \
  --url 'http://192.168.1.3:8000/media/fairplay/fps-hls.html' \
  -- --unsafely-treat-insecure-origin-as-secure=http://192.168.1.3:8000 \
  2>&1 | tee /tmp/cobalt_fps_hls_bridge_review_20260525_120310.log
```

Device log:

```text
/tmp/cobalt_device_debug_20260525_120428.log
```

What worked:

```text
Derived IV from media sequence 0
Bridging SAMPLE-AES metadata to parser role=primary key_id_size=6 iv_size=16 uri=skd://twelve
ManifestDemuxer::SetEncryptionInfo role=primary scheme=2
ChunkDemuxer::SetEncryptionInfo id=primary scheme=2 key_id_size=6 iv_size=16
Mp2tStreamParser::SetHlsSampleAesEncryption scheme=2 key_id_size=6 iv_size=16
...
AudioDecoderConfig ... encryption scheme: CBCS
VideoDecoderConfig ... encryption scheme: CBCS
```

This confirms the bridge closes the previous `AudioDecoderConfig` /
`VideoDecoderConfig` `Unencrypted` gap.

New run result:

```text
PROMISE: requestMediaKeySystemAccess resolved keySystem=com.youtube.fairplay.sbdl
PROMISE: createMediaKeys resolved
PROMISE: setServerCertificate resolved
EVENT(message-debug) messageType=license-request bytes=5632
FETCH: CKC response decoded bytes=556
PROMISE: session.update() resolved
PROMISE: video.setMediaKeys resolved
CDM set successfully.
didFailWithError: identifier='skd://twelve' error: Error Domain=CoreMediaErrorDomain Code=-42681
```

Notably, this clean run did not reach the previous `Video WriteSample` log
point before `AVContentKeySession` failed with `-42681`. The CKC response also
showed the old `0xcd` pattern:

```text
Hex prefix: 0000000000b4d400ffffffffff4b2bffcdcdcdcdcdcd...
```

Interpretation:

- The bridge successfully propagates HLS SAMPLE-AES metadata into the MP2T
  parser far enough for encrypted decoder configs.
- The immediate failure moved from `drm_info=null` / unencrypted configs to
  FairPlay CKC/content-key acceptance for the local `skd://twelve` fixture.
- The next debug step is to inspect the local CKC response handling and
  `updateSession` copy/lifetime path for this fixture before evaluating
  `AVSampleBufferAttachContentKey` suitability again.

Review findings for `aa909f5e309c1`:

1. `hls_rendition_impl.cc` parses a 32-character SKD UUID using `std::stoi`.
   A malformed manifest URI with 32 non-hex characters can throw/terminate in
   Chromium's no-exceptions build style. Use Chromium hex parsing helpers and
   reject invalid hex without exceptions.
2. `SAMPLE-AES-CTR` and `SAMPLE-AES-CENC` are bridged as
   `EncryptionScheme::kCbcs`. `SAMPLE-AES-CTR` is documented in
   `tags.h` as Common Encryption `cenc` / AES-CTR, so it should map to
   `EncryptionScheme::kCenc`; `ISO-23001-7` is handled by the pass-through path
   but not by the bridge.
3. `METHOD=NONE` after protected segments does not clear the parser's HLS
   encryption state. A mixed clear/protected playlist could continue tagging
   clear samples with the previous decrypt config unless the bridge explicitly
   resets to `EncryptionScheme::kUnencrypted`.

Actual MP4 protection metadata captured from Chromium's fMP4 parser:

```text
Log: /tmp/cobalt_mp4_sinf_log.log
MP4 video protection:
  original_format=avc1 scheme=cbcs scheme_version=65536
  default_iv_size=0
  default_kid=302F80DD411E4886BCA5BB1F8018A024
  crypt_block=1 skip_block=9
  constant_iv=77FD1889AAF4143B085548B3C0F95B9A

MP4 audio protection:
  original_format=mp4a scheme=cbcs scheme_version=65536
  default_iv_size=0
  default_kid=302F80DD411E4886BCA5BB1F8018A024
  crypt_block=0 skip_block=0
  constant_iv=77FD1889AAF4143B085548B3C0F95B9A
```

This confirms the second synthesized audio `sinf` was structurally aligned with
the real init segment for audio.

Audio `enca` subtype probe:

```text
Log: /tmp/cobalt_audio_enca.log
Audio: Sample suitability metadata
  format_subtype=enca(0x656e6361)
  SampleDescriptionExtensionAtoms = { sinf = length 89 }
Result: AVSampleBufferAttachContentKey still fails:
  AVFoundationErrorDomain -11836 / NSOSStatusErrorDomain -12161
```

This ruled out the most obvious protected sample-entry subtype mismatch for
audio. At this point the evidence says a manually built `CMAudioSampleBuffer`
with matching `enca`/`sinf`/`tenc`, matching KID, matching constant IV, matching
cryptor byte ranges, and a present `AVContentKey` still fails Apple's kernel
suitability check.

## Next Debugging Candidates

1. Determine whether AVSBDL manual FairPlay attachment can be made to work for
   MPEG-2 TS SAMPLE-AES elementary samples without preserving container/manifest
   protection metadata in the `CMFormatDescription`. Current evidence suggests
   we need to propagate a richer protected sample description, not only
   `SbDrmSampleInfo`.
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
- Apple `AVSampleBufferAttachContentKey`
- Apple `kCMSampleAttachmentKey_CryptorSubsampleAuxiliaryData`
- Apple Technical Note TN2454, "Debugging FairPlay Streaming"
- WebKit `SourceBufferParserAVFObjC` / CoreMedia soft-link references for
  `kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms` and
  `kCMFormatDescriptionExtension_ProtectedContentOriginalFormat`
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

## WebKit `fps-hls.html` In Cobalt Deploy Attempt - May 25, 2026

Related setup details are in [WebKit FairPlay Layout Test Setup Guide](WebKit-FairPlay-LayoutTest-Setup-Guide.md#14-cobalt-deploy-attempt-with-webkit-page).

Command shape used:

```bash
./deploy_and_run_cobalt_device.sh \
  --bundle-id 'abhijeet.tvos.org.chromium.chrome.unittests.dev' \
  --device 'Abhijeet-TVOS' \
  --url 'http://192.168.1.3:8000/media/fairplay/fps-hls.html'
```

What worked:

- Cobalt navigated to the WebKit layout-test page served from the Mac.
- Apple TV (`192.168.1.7`) fetched `fps-hls.html`, WebKit helper scripts, `prog_index.m3u8`, and `main.ts`.
- Cobalt parsed `EXT-X-KEY` and fired manifest-level SKD init data for `skd://twelve`.

Key log:

```text
[ABHIJEET][HLS] EXT-X-KEY parsed: method=3 uri=skd://twelve keyformat=3 has_iv=0
[ABHIJEET][HLS] Firing SKD init data from manifest size=12 uri=skd://twelve
[ABHIJEET][FPS-FLOW] WebMediaPlayerImpl::OnEncryptedMediaInitData: type=5 size=12
```

What did not work:

- No POST to WebKit `resources/index.py` was observed in the local Apache access log.
- Starboard sample writes reported `drm_system=null drm_info=null has_drm=0`.
- Playback failed with AVSBDL decode error `AVFoundationErrorDomain -11821`, underlying `NSOSStatusErrorDomain -12909`, and Chromium `PIPELINE_ERROR_DECODE`.

Current conclusion:

The WebKit page can be used as a Cobalt deploy target, but this run did not reach license handshaking. The next Cobalt-side gap is preserving/enriching encryption metadata from HLS SAMPLE-AES MPEG-TS parsing into `DecoderBuffer` / `SbDrmSampleInfo`; the manifest-level SKD event alone is not enough when the samples are delivered as `has_drm=0`.

## WebKit CKC Stub Server Venv Fix - May 25, 2026

Related setup details are in [WebKit FairPlay Layout Test Setup Guide](WebKit-FairPlay-LayoutTest-Setup-Guide.md#15-running-the-webkit-ckc-stub-with-a-python-venv).

User Safari run reached the CKC request stage:

```text
EVENT(message)
PROMISE: licenseResponse resolved
Server returned malformed response: SyntaxError: The string did not match the expected pattern. FAIL
```

Apache confirmed the POST, but the CGI returned HTTP 500:

```text
POST /media/fairplay/resources/index.py HTTP/1.1" 500
ValueError: mutable default <class 'bytearray'> for field contentKey is not allowed: use default_factory
```

What worked:

- WebKit source was left unchanged.
- A Python 3.9 venv was created at `/tmp/webkit-fps-cgi-venv`.
- Apache was restarted so `#!/usr/bin/env python3` resolves to the venv Python.
- `DISABLE_WEBKITCOREPY_AUTOINSTALLER=1` was injected with `SetEnvIf`, letting the venv's installed `pycryptodome` satisfy `Crypto` imports.
- A fake-SPC POST now returns HTTP 400 instead of HTTP 500, which means the keyserver imports and reaches SPC parsing.

What did not work:

- Starting Apache with only the venv `PATH` was insufficient. The CGI used venv Python, but WebKit's autoinstall hook still intercepted `Crypto` and tried to install `pycryptodome-3.10.1`.
- `PassEnv` and `SetEnv` could not be used because this Apache config does not load `mod_env`.

Validation result:

```text
HTTP/1.1 400 Bad Request
Bad request

Request to set autoinstall directory to /Users/abhijeet/code/WebKit/Tools/Scripts/libraries/autoinstalled/python-3-arm64
Environment variable DISABLE_WEBKITCOREPY_AUTOINSTALLER=1 overriding request
Received error when parsing ckcData: ... AssertionError
```

The `AssertionError` is expected for the deliberately invalid `spc:"AAAA"` probe. The important change is that the failure moved from Python environment/import setup to actual SPC parsing.

## Standalone FairPlay Test Resource - May 25, 2026

Created local independent test fixture:

```text
fairplay-fps-hls-test-resource/
```

It includes copied WebKit `fps-hls.html` assets, HLS content, certificate, CKC keyserver, `requirements.txt`, and `serve_fps_hls.py`. The source references for borrowed WebKit assets are recorded in `fairplay-fps-hls-test-resource/SOURCE-REFERENCES.md` and in comments on copied text entry points.

Verification with a fresh Python 3.9 venv:

```text
python -m py_compile serve_fps_hls.py resources/index.py resources/keyserver/__init__.py -> pass
GET fps-hls.html -> 200 OK
GET prog_index.m3u8 -> 200 OK
Range GET main.ts bytes=0-99 -> 206 Partial Content
POST fake SPC -> 400 Bad Request
Semgrep scan of local server wrapper -> no findings
```

Important local fixture patch:

- The copied `resources/keyserver/__init__.py` now skips WebKit's `webkitcorepy` autoinstall registration when `DISABLE_WEBKITCOREPY_AUTOINSTALLER` is set. This is required for independence from the WebKit checkout; the standalone server sets that environment variable before invoking the CKC subprocess.

## Cobalt Run Against Standalone Resource - May 25, 2026

Standalone server:

```text
Directory: /Users/abhijeet/code/cobalt-github/src/fairplay-fps-hls-test-resource
URL:       http://192.168.1.3:8000/media/fairplay/fps-hls.html
Server:    .venv/bin/python serve_fps_hls.py --host 0.0.0.0 --port 8000
```

Deploy command:

```bash
./deploy_and_run_cobalt_device.sh \
  --bundle-id 'abhijeet.tvos.org.chromium.chrome.unittests.dev' \
  --device 'Abhijeet-TVOS' \
  --url 'http://192.168.1.3:8000/media/fairplay/fps-hls.html' \
  2>&1 | tee /tmp/cobalt_fps_hls_standalone_resource.log
```

Cobalt log files:

```text
/tmp/cobalt_fps_hls_standalone_resource.log
/tmp/cobalt_device_debug_20260525_094622.log
```

Server evidence:

- Safari on the Mac reached the full license path before the Cobalt run:
  - `GET /media/fairplay/resources/cert.der -> 200`
  - `POST /media/fairplay/resources/index.py -> 200`
- Apple TV / Cobalt (`192.168.1.7`) loaded the page and media:
  - `GET /media/fairplay/fps-hls.html -> 200`
  - `GET /media/fairplay/support.js -> 200`
  - `GET /media/fairplay/eme2016.js -> 200`
  - `GET /media-resources/video-test.js -> 200`
  - `GET /media/fairplay/content/prog_index.m3u8 -> 206`
  - `GET /media/fairplay/content/main.ts -> 206`
- No Cobalt `POST /media/fairplay/resources/index.py` was observed.
- A `BrokenPipeError` occurred while serving `main.ts` to Cobalt after the client disconnected. This is consistent with Cobalt aborting playback after the AVSBDL decode error, not a CKC server failure.

Cobalt evidence:

```text
Navigated to http://192.168.1.3:8000/media/fairplay/fps-hls.html
[ABHIJEET][HLS] EXT-X-KEY parsed: method=3 uri=skd://twelve keyformat=3 has_iv=0
[ABHIJEET][HLS] Firing SKD init data from manifest size=12 uri=skd://twelve
[ABHIJEET][HLS] ManifestDemuxer::OnEncryptedMediaInitData type=5 data_size=12
[ABHIJEET][FPS-FLOW] DemuxerManager::OnEncryptedMediaInitData: type=5 size=12 client=set
[ABHIJEET][FPS-FLOW] WebMediaPlayerImpl::OnEncryptedMediaInitData: type=5 size=12
[ABHIJEET][FPS-FLOW] Video WriteSample: drm_system=null drm_info=null has_drm=0
AVSBDL decode failed ... AVFoundationErrorDomain -11821, NSOSStatusErrorDomain -12909
SetError: {code=3, message="PIPELINE_ERROR_DECODE: AVSBDL decode failed ..."}
```

Corrected conclusion after inspecting the page DOM through DevTools:

The standalone server and Safari CKC path are working. Cobalt still does not reach the license POST because the page fails at the EME API entry point:

```text
EVENT(encrypted)
TypeError: navigator.requestMediaKeySystemAccess is not a function FAIL
END OF TEST
```

CDP verification against `192.168.1.7:9222/devtools/page/...` (WebSocket) returned the same result when directly evaluating the API call:

```json
{
  "ok": false,
  "name": "TypeError",
  "message": "navigator.requestMediaKeySystemAccess is not a function"
}
```

The media pipeline still independently continues far enough to parse the HLS `EXT-X-KEY`, fire manifest-level SKD init data, and write TS-derived samples as unencrypted (`drm_system=null`, `drm_info=null`, `has_drm=0`), causing the AVSBDL decode failure. But the immediate JavaScript/handshake blocker is earlier: `navigator.requestMediaKeySystemAccess` is not exposed in this Cobalt page context.

Next investigation point:

- Determine why EME is unavailable in this Cobalt build/page context despite HLS encrypted init data reaching `WebMediaPlayerImpl`.
- Search for feature flags, build args, runtime switches, or Cobalt/Chromium patches that disable `navigator.requestMediaKeySystemAccess`.
- After EME is exposed, re-run the standalone fixture and only then continue CKC/server or sample metadata debugging.

## 2026-05-25 Cobalt Run With `com.youtube.fairplay.sbdl`

Test page:

```text
http://192.168.1.3:8000/media/fairplay/fps-hls.html
```

Run command:

```bash
./deploy_and_run_cobalt_device.sh \
  --bundle-id 'abhijeet.tvos.org.chromium.chrome.unittests.dev' \
  --device 'Abhijeet-TVOS' \
  --url 'http://192.168.1.3:8000/media/fairplay/fps-hls.html' \
  2>&1 | tee /tmp/cobalt_fps_hls_sbdl_console_20260525_102626.log
```

Logs:

```text
/tmp/cobalt_fps_hls_sbdl_console_20260525_102626.log
/tmp/cobalt_device_debug_20260525_102721.log
```

What worked:

- The standalone server served the updated page to Cobalt.
- The page started and wrote console logs into the Cobalt device log.
- The HLS manifest parser detected FairPlay/SAMPLE-AES metadata:

```text
[ABHIJEET][HLS] EXT-X-KEY parsed: method=3 uri=skd://twelve keyformat=3 has_iv=0
[ABHIJEET][HLS] OnMediaPlaylist: DRM detected method=3 keyformat=3 uri=skd://twelve role=primary
[ABHIJEET][HLS] Firing SKD init data from manifest size=12 uri=skd://twelve
[ABHIJEET][HLS] ManifestDemuxer::OnEncryptedMediaInitData type=5 data_size=12
[ABHIJEET][FPS-FLOW] DemuxerManager::OnEncryptedMediaInitData: type=5 size=12 client=set
[ABHIJEET][FPS-FLOW] WebMediaPlayerImpl::OnEncryptedMediaInitData: type=5 size=12
```

- The JavaScript page received the encrypted event:

```text
START: fps-hls standalone FairPlay test
EVENT(encrypted) initDataType=skd initDataBytes=12 keyURI=skd://twelve
```

What failed:

- The EME entry point is still not exposed in this Cobalt page context, even when the page uses Cobalt's expected key system name `com.youtube.fairplay.sbdl`:

```text
CHECK: typeof navigator.requestMediaKeySystemAccess=undefined
CALL: requestMediaKeySystemAccess keySystem=com.youtube.fairplay.sbdl config={"initDataTypes":["skd"],"videoCapabilities":[{"contentType":"application/vnd.apple.mpegurl","robustness":""}],"distinctiveIdentifier":"not-allowed","persistentState":"not-allowed","sessionTypes":["temporary"]}
FAIL: requestMediaKeySystemAccess rejected name=TypeError message=navigator.requestMediaKeySystemAccess is not a function TypeError: navigator.requestMediaKeySystemAccess is not a function
```

- Because `requestMediaKeySystemAccess` is missing, Cobalt does not reach:
  - `createMediaKeys`
  - certificate fetch from `resources/cert.der`
  - `setServerCertificate`
  - `session.generateRequest`
  - CKC POST to `resources/index.py`
  - `session.update`
  - `video.setMediaKeys`

Independent lower-pipeline failure still observed:

```text
[ABHIJEET][FPS-FLOW] Video WriteSample: drm_system=null drm_info=null has_drm=0
EVENT(video.error) code=3 message=PIPELINE_ERROR_DECODE: AVSBDL decode failed ... AVFoundationErrorDomain Code=-11821 ... NSOSStatusErrorDomain Code=-12909
```

Conclusion:

The current first handshake blocker is not the CKC stub server and not the key-system string in the page. Cobalt successfully reaches the `encrypted` event with SKD init data, but Blink/Cobalt does not expose `navigator.requestMediaKeySystemAccess`, so the EME handshake cannot start. The AVSBDL decode failure is still important, but it is downstream of the missing JavaScript EME API for this test page.

Next step:

Investigate why EME is disabled or not installed on `Navigator` in this build. Search focus should be `requestMediaKeySystemAccess`, `EncryptedMedia`, `MediaKeys`, Cobalt feature flags, and the runtime/build path that controls EME exposure separately from key-system registration.

## Safari vs Cobalt Server-Side Comparison - May 25, 2026

### Setup

- Server: WebKit stub FairPlay key server (`serve_fps_hls.py` + `index.py`)
- Python: 3.9 venv (required; Python 3.14 fails on WebKit dataclass defaults)
- Test page: `fps-hls.html` with multi-key-system support
  (`com.apple.fps`, `com.youtube.fairplay.sbdl`, `com.youtube.fairplay`)
- Server logs: `/tmp/fps_server.log` with `[FPS-SERVER]` diagnostic tags
- Server captures: User-Agent, SPC size/prefix, CKC size/prefix per request

### Safari Baseline (Working)

```text
Server log: /tmp/fps_server.log (12:47:26 IST)

[FPS-SERVER] === License request ===
[FPS-SERVER] User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15
[FPS-SERVER] Content-Length: 10174
[FPS-SERVER] uri=skd://twelve spc_b64_len=10072
[FPS-SERVER] === New license request ===
[FPS-SERVER] assetId=twelve
[FPS-SERVER] spc_size=7552 spc_prefix=00000001000000002a88cc9594e073f069105973133088c0...
[FPS-SERVER] Response status=200 body_len=1451
[FPS-SERVER] CKC b64_len=1384 raw_len=1036 prefix=0000000100000000603ec180acf2c5de...

Safari page output:
  PROMISE: requestMediaKeySystemAccess resolved keySystem=com.apple.fps
  PROMISE: session.generateRequest resolved
  EVENT(message-debug) messageType=license-request bytes=7584
  PROMISE: licenseResponse resolved
  FETCH: CKC response decoded bytes=1036
  PROMISE: session.update() resolved
  (video plays, segments fetched)
```

Safari summary:
- Key system: `com.apple.fps`
- SPC: 7552 raw bytes, prefix `00000001 00000000`
- CKC: 1036 raw bytes, prefix `00000001 00000000`
- Server returned HTTP 200 with valid JSON
- Playback started (`.ts` segment requests followed the CKC)

### Cobalt Comparison (Pending)

To be captured after deploying Cobalt with:
```bash
./deploy_and_run_cobalt_device.sh \
  --bundle-id 'abhijeet.tvos.org.chromium.chrome.unittests.dev' \
  --device 'Abhijeet-TVOS' \
  --url 'http://192.168.1.3:8000/media/fairplay/fps-hls.html' \
  -- --unsafely-treat-insecure-origin-as-secure=http://192.168.1.3:8000 \
  2>&1 | tee /tmp/cobalt_fps_safari_compare.log
```

Expected comparison points:
- SPC size and prefix (should match Safari's `00000001 00000000` pattern)
- CKC size and prefix (should be identical since same server/key)
- Whether Cobalt's JS receives the same CKC bytes that Safari does
- Whether `processContentKeyResponse:` succeeds with the same CKC
- If `-42681` persists, the difference is in how Cobalt delivers the CKC
  to `AVContentKeyRequest`, not in the CKC data itself


### Bugs Found and Fixed During Server Comparison (May 25, 2026)

Three CKC corruption sources were identified by comparing server-sent bytes
against Cobalt-received bytes using the `[FPS-SERVER]` diagnostic logging:

**Bug 1: Async pointer use-after-free (root cause of `71cec6ff` / `0xcdcd` corruption)**

`FairplayKeySystem::updateSession` copied the caller-owned `key` pointer
inside `dispatch_async`. By the time the block ran, the pointer was freed.
The `NSData dataWithBytes:key` inside the block read garbage memory.

Fix: Copy `key` bytes into `NSData` BEFORE `dispatch_async`:
```objc
// BEFORE dispatch:
NSData* rawLicCopy = [NSData dataWithBytes:key length:keySize];
dispatch_async(_dispatchQueue, ^{
    NSData* rawLic = rawLicCopy;  // safe captured copy
    ...
});
```

**Bug 2: Message ID patching (bytes 8-23 overwritten)**

When `spcWasWrapped` was true, the code replaced bytes 8-23 of the CKC with
the SPC message ID. This corrupted the CKC's internal structure. The server
comparison showed the prefix changing from the server's `2d147b2e` to
`ee26d99e` (the SPC message ID).

Fix: Removed message ID patching entirely. The CKC is generated by the server
for a specific SPC and must not be modified.

**Bug 3: StripAxinomHeader incorrectly stripping valid CKC**

`StripAxinomHeader` detected the `00000001 00000000` prefix (which is the
standard FairPlay CKC version header) and stripped 28 bytes. This turned a
valid 620-byte CKC into an invalid 592-byte payload.

Fix: Removed all CKC stripping. Raw CKC from the license server is passed
directly to `processContentKeyResponse`.

**Bug 4: Deferred fulfillment never triggered**

After removing `AddContentKeyRecipient` from renderers (WebKit pattern),
the deferred fulfillment's trigger (`_hardwareRecipientCount > 0`) was
never satisfied. `processContentKeyResponse` was never called, so the key
was never fulfilled, so `Decrypt()` returned `kRetry`, so samples never
flowed to the player.

Fix: Call `processContentKeyResponse` immediately when `updateSession`
receives the CKC (like Safari does). No deferral needed.

**Result after all four fixes:**

```text
Server CKC:  raw_len=1260 prefix=000000010000000096678ca24aa65d07
Native CKC:  keySize=1260 rawPrefix=000000010000000096678ca24aa65d07
              CKC passed through unchanged: 1260 bytes
              (no didFailWithError, no -42681)
```

The `-42681` error is eliminated. The FairPlay handshake completes
successfully. The remaining stall at `kSbPlayerStatePrerolling` is due to
the test page missing `autoplay muted` (fixed) and pipeline instrumentation
is being added to trace sample flow from demuxer to AVSBDL renderers.

### Cobalt Comparison Captured (14:02 IST)

Logs:
- Server: `/tmp/fps_server.log`
- Cobalt combined: `/tmp/cobalt_fps_safari_compare.log`
- Device console: `/tmp/cobalt_device_debug_20260525_140253.log`

What worked:

```text
EVENT(encrypted) initDataType=skd initDataBytes=12 keyURI=skd://twelve
PROMISE: requestMediaKeySystemAccess resolved keySystem=com.youtube.fairplay.sbdl
PROMISE: createMediaKeys resolved
PROMISE: setServerCertificate resolved
PROMISE: session.generateRequest resolved
EVENT(message-debug) messageType=license-request bytes=7424
FETCH: CKC response decoded bytes=988
PROMISE: session.update() resolved
PROMISE: video.setMediaKeys resolved
CDM set successfully.
```

Server/native byte comparison:

```text
Server SPC:  raw_len=7424 prefix=00000001000000005a3b48f001a6a870...
Native SPC:  message size=7424 prefix=00000001000000005a3b48f001a6a870...
Server CKC:  raw_len=988  prefix=00000001000000007faf3f22184dabce
JS CKC:      decoded bytes=988
Native CKC:  keySize=988 rawPrefix=00000001000000007faf3f22184dabce
```

Conclusion:

The earlier CKC byte-corruption suspicion is not reproduced in the 14:02 run.
The server CKC, JavaScript-decoded CKC length, and native `updateSession` CKC
prefix match. The FairPlay/EME handshake now reaches `session.update()`,
`video.setMediaKeys()`, and `CDM set successfully`.

What failed:

```text
StarboardRenderer::OnPlayerStatus() called with kSbPlayerStatePrerolling
```

The log stops there. There are no `Video WriteSample`, `Audio:
CryptorSubsampleAuxiliaryData`, or `AttachContentKey` lines in this run. That
means the current stall is after CDM attachment and player creation, before
samples reach the AVSBDL audio/video renderers.

Next instrumentation added:

- `SbPlayerBridge::OnDecoderStatus`: player/ticket matching and data requests.
- `StarboardRenderer::OnNeedData`: whether Cobalt issues demuxer reads or
  incorrectly treats the HLS path as URL-player-buffered.
- `StarboardRenderer::OnDemuxerStreamRead`: demuxer status, buffer counts,
  timestamps, and decrypt-config sizes.
- `SbPlayerBridge::WriteBuffersInternal`: buffers passed into
  `SbPlayerWriteSamples`, including DRM key-id/IV/subsample metadata.

Next build/run should answer whether the preroll stall is:

1. No decoder-status callback from the platform player.
2. Decoder status received, but `OnNeedData` is suppressed.
3. Demuxer read pending or returning no buffers.
4. Buffers are written to Starboard but rejected/deferred before AVSBDL.

## Implementation Reference for Re-implementation (May 25, 2026)

This section documents every change made to `DrmSystemFairplay` / `FairplayKeySystem`
in `cobalt/internal/starboard/shared/tvos/drm_system_fairplay.mm` so the implementation
can be reproduced from scratch if needed.

### 1. State Machine

Added `FairplaySessionState` enum to `FairplayKeySystemSession`:

```objc
typedef NS_ENUM(NSInteger, FairplaySessionState) {
  kStateIdle,
  kStateWaitingForSPC,     // processContentKeyRequestWithIdentifier called
  kStateWaitingForLicense,  // SPC generated, waiting for JS to call updateSession
  kStateLicenseCached,      // CKC received but not yet delivered to Apple
  kStateFulfilling,         // processContentKeyResponse called, waiting for didProvideContentKey
  kStateReady,              // AVContentKey available
  kStateError
};
```

Added properties to `FairplayKeySystemSession`:
- `state` (FairplaySessionState)
- `licenseData` (NSData) - cached CKC for deferred fulfillment
- `contentIdentifier` (NSData) - content ID for SPC generation
- `fulfillmentStartTime` (NSDate) - for timeout diagnostics

### 2. Non-UUID Key ID Support

In `generateSessionUpdateRequest:type:initData:sessionId:`, when the `skd://` URI
contains a non-UUID key ID (e.g., `skd://twelve`):

```objc
NSUUID* uuid = [[NSUUID alloc] initWithUUIDString:uuidStr];
if (uuid) {
  uuid_t ub; [uuid getUUIDBytes:ub];
  keyId = [NSData dataWithBytes:ub length:16];
} else {
  // Non-UUID: use raw UTF-8 bytes to match what the TS parser bridge stores
  keyId = [uuidStr dataUsingEncoding:NSUTF8StringEncoding];
}
```

This is essential because the HLS-to-MP2T bridge stores the same raw string
as the `DecryptConfig` key ID. The `Decrypt()` gate and `GetContentKey()` both
match by comparing `session.keyId` against the sample's `drm_info->identifier`.

### 3. CKC Passthrough (Three Corruption Fixes)

**Fix 1: Async pointer copy** - Copy `key` bytes BEFORE `dispatch_async`:
```objc
- (void)updateSession:(int)ticket key:(const void*)key keySize:(int)keySize ... {
  NSData* rawLicCopy = [NSData dataWithBytes:key length:keySize]; // BEFORE dispatch
  dispatch_async(_dispatchQueue, ^{
    NSData* rawLic = rawLicCopy; // safe captured copy
```
Without this, the `key` pointer (caller-owned) is freed before the async block runs.

**Fix 2: No message ID patching** - Removed code that replaced bytes 8-23 of CKC
with SPC message ID. The CKC is generated by the server for a specific SPC and must
not be modified.

**Fix 3: No header stripping** - Removed `StripAxinomHeader`. The `00000001 00000000`
prefix is the standard FairPlay CKC version header, not an Axinom wrapper. Stripping
28 bytes corrupted valid CKC data.

Final CKC handling:
```objc
session.licenseData = rawLic; // pass through unchanged
```

### 4. Immediate Fulfillment

Call `processContentKeyResponse` immediately when `updateSession` receives the CKC.
Do NOT defer until hardware recipients are registered:

```objc
if (session.keyRequest) {
  session.state = kStateFulfilling;
  [session.keyRequest processContentKeyResponse:
      [AVContentKeyResponse contentKeyResponseWithFairPlayStreamingKeyResponseData:session.licenseData]];
} else {
  session.state = kStateLicenseCached;
}
```

Safari calls `processContentKeyResponse` immediately. Deferral caused the key to
never be fulfilled because `AddContentKeyRecipient` was removed (WebKit pattern).

### 5. Decrypt Gate

```objc
SbDrmSystemPrivate::DecryptStatus DrmSystemFairplay::Decrypt(InputBuffer* b) {
  if (!b || !b->drm_info() || b->drm_info()->identifier_size <= 0)
    return kSuccess; // unencrypted sample
  NSData* kid = [NSData dataWithBytes:b->drm_info()->identifier
                               length:b->drm_info()->identifier_size];
  return [fairplay_key_system_ canPassEncryptedSamplesForKeyId:kid]
      ? kSuccess : kRetry;
}
```

`canPassEncryptedSamplesForKeyId:` checks `session.licenseData != nil && state != kStateError`.

### 6. Content Key Retrieval

```objc
AVContentKey* DrmSystemFairplay::GetContentKey(const uint8_t* k, int s) {
  return [fairplay_key_system_ contentKeyForKeyId:[NSData dataWithBytes:k length:s]];
}
```

`contentKeyForKeyId:` iterates sessions, matches `keyId`, and returns
`session.keyRequest.contentKey` if available. Sets `state = kStateReady` on first access.

### 7. Identifier Mapping

For `skd://` URIs, the `processContentKeyRequestWithIdentifier:` uses the full URI
as the AVContentKeyRequest identifier, but the session is keyed by ticket number:

```objc
_identifierToSessionId[keyRequestIdentifier] = sessionId;
```

The `didProvideContentKeyRequest:` delegate resolves back:
```objc
NSString* mapped = _identifierToSessionId[requestIdentifier];
if (mapped) sessionId = mapped;
```

### 8. DrmSystemPlatform Stub Removal

`starboard/tvos/shared/media/drm_system_platform.mm` had stub implementations
returning `false`/`""` for `IsKeySystemSupported`, `IsSupported`, `GetName`,
`GetKeySystemName`. These were only compiled in non-internal builds (`else` branch
in `BUILD.gn`), but stale ninja build files caused the linker to pick the stubs
over the real implementations from `drm_system_fairplay.mm`.

Fix: `gn gen` to regenerate build files after any GN config change. The stubs
are correctly excluded when `COBALT_USE_INTERNAL_BUILD=1`.

### 9. Remaining Blocker: AVSampleBufferAttachContentKey -12161

The full pipeline works through:
- HLS manifest parsing (EXT-X-KEY SAMPLE-AES)
- EME handshake (SPC/CKC with WebKit stub server, Safari-verified)
- Encryption bridge (CBCS config on DecoderConfig)
- Decrypt gate passes (key ID matches)
- Content key found (AVContentKey present)
- `AVSampleBufferAttachContentKey` called

But Apple rejects the attachment with `-12161`. Web search confirms
`addContentKeyRecipient` on `AVSampleBufferDisplayLayer`/`AVSampleBufferAudioRenderer`
may be required BEFORE `processContentKeyResponse` for manually-built `CMSampleBuffer`
objects. WebKit avoids this because it uses `AVStreamDataParser` (Apple's native
parser) which handles recipient registration internally.

Next steps:
1. Re-add `addContentKeyRecipient` on renderers with correct ordering
   (register BEFORE `processContentKeyResponse`, not after)
2. If still fails, investigate `AVStreamDataParser` integration (Option A)
3. If still fails, minimal native harness to isolate the issue

### Files Changed Summary

| File | Repo | Change |
|------|------|--------|
| `drm_system_fairplay.h` | internal | Decrypt signature, AddContentKeyRecipient decl |
| `drm_system_fairplay.mm` | internal | Full rewrite: state machine, CKC passthrough, non-UUID keyId, immediate fulfillment, Decrypt gate |
| `drm_system_platform.mm` | cobalt-github | Stub removal (excluded by GN in internal build) |
| `application_drm_system.h/.mm` | cobalt-github | AVContentKeySessionDelegate, manual key request trigger for AVSBDL |
| `fairplay_key_system_info.h/.cc` | cobalt-github | FairplayKeySystemInfoSBDL subclass |
| `cobalt_content_renderer_client.cc` | cobalt-github | Register com.youtube.fairplay.sbdl |
| `media_is_key_system_supported.mm` | cobalt-github | Allow H.264 for SBDL key system |
| `hls_rendition_impl.cc` | cobalt-github | SAMPLE-AES metadata bridge (key ID + IV extraction) |
| `chunk_demuxer.cc/.h` | cobalt-github | SetEncryptionInfo forwarding |
| `manifest_demuxer.cc/.h` | cobalt-github | SetEncryptionInfo in host interface |
| `mp2t_stream_parser.cc/.h` | cobalt-github | SetHlsSampleAesEncryption public setter |
| `hls_manifest_demuxer_engine.cc/.h` | cobalt-github | SKD init data from manifest, SAMPLE-AES passthrough |
| `media_playlist.cc` | cobalt-github | Accept skd:// URIs for DRM methods |
| `hls_network_access_impl.cc` | cobalt-github | Skip HTTP key fetch for DRM methods |
| `web_media_player_impl.cc` | cobalt-github | ManifestDemuxer in CanPlayThrough |

---

## Fresh Branch Re-implementation Session (May 25, 2026 evening)

This section documents the build-deploy-analyze-fix cycle on the `HTML5Player-01` branch,
starting from a clean state with the HLS demuxer changes committed and the FairPlay DRM
layer needing re-implementation.

### Issue 1: EME API Undefined (`navigator.requestMediaKeySystemAccess is not a function`)

**Symptom**: JS console logged `typeof navigator.requestMediaKeySystemAccess=undefined` and
all three key systems (`com.apple.fps`, `com.youtube.fairplay.sbdl`, `com.youtube.fairplay`)
failed with "navigator.requestMediaKeySystemAccess is not a function".

**Root cause**: The EME `requestMediaKeySystemAccess` IDL has `[SecureContext]` attribute.
The test page was served over HTTP (`http://192.168.1.3:8000`), which is not a secure context.
The deploy command was missing the `--unsafely-treat-insecure-origin-as-secure` flag.

**Why we hit it**: The standalone resource deploy command in the doc (line 958) did not
include the flag, while earlier deploy commands (lines 559, 674) did. Copy-paste from the
wrong section.

**Fix**: Add `-- --unsafely-treat-insecure-origin-as-secure=http://192.168.1.3:8000` to
the deploy command.

### Issue 2: Key ID Mismatch (Decrypt gate returning kRetry forever)

**Symptom**: After EME handshake succeeded (SPC/CKC exchange, `session.update()` resolved,
`has_additional_usable_key: true`), the player wrote encrypted samples but got stuck in
`kSbPlayerStatePrerolling`. No error, no progress. Added logging revealed
`Decrypt: kRetry - no content key for key_id_size=6` repeating every ~9ms.

**Root cause**: The `_contentKeys` dictionary was keyed by the EME init data
(`"skd://twelve"` = 12 bytes), but the `Decrypt` function looked up by the TS parser's
`drm_info->identifier` (`"twelve"` = 6 bytes). The `skd://` prefix was included in the
DRM system's `keyId` but stripped by the HLS-to-MP2T bridge in `hls_rendition_impl.cc`.

**Why we hit it**: Two independent code paths extract the key ID differently:
- `hls_rendition_impl.cc` strips `skd://` prefix at line 493-514 (correct, per HLS spec)
- `drm_system_fairplay.mm` stored raw EME init data as `keyId` (includes prefix)

These were written at different times without a shared key ID normalization step.

**Fix**: Strip `skd://` prefix in `generateSessionUpdateRequest` before storing as `keyId`.
Added separate `contentIdentifier` property on `FairplayKeySystemSession` to preserve the
full URI for `makeStreamingContentKeyRequestDataForApp:contentIdentifier:`.

```objc
static const char kSkdPrefix[] = "skd://";
if (initializationDataSize > kSkdPrefixLen &&
    memcmp(initializationData, kSkdPrefix, kSkdPrefixLen) == 0) {
  keyId = [NSData dataWithBytes:remainder length:remainderLen];
}
```

### Issue 3: AVSampleBufferAttachContentKey -12161

**Symptom**: After fixing key ID mismatch, `Decrypt: kSuccess` for all samples, but audio
renderer immediately failed: `Failed to attach content key. Error -11836, underlying_err:
NSOSStatusErrorDomain Code=-12161`.

**Root cause**: The original Cobalt code used per-sample `AVSampleBufferAttachContentKey`
(designed for the DASH/Widevine path). For FairPlay SAMPLE-AES with manually-built
CMSampleBuffers, this API requires a **private entitlement** that only Apple extension
processes have.

**Why we hit it**: The original AVSBDL DRM code was designed for YouTube's DASH content
(`com.youtube.fairplay.sbdl` key system) where content arrives as MP4 fragments in AVCC
format. It was never tested with FairPlay SAMPLE-AES from HLS MPEG-TS.

**Evidence**: WebKit PR #21770 (`[iOS] Enable SampleBufferContentKeySessionSupportEnabled
by default`) shows two mutually exclusive approaches:
1. `AVSampleBufferAttachContentKey` - per-sample, only in extension processes
   (`supportsAttachContentKey()` checks `processIsExtension()`)
2. `addContentKeyRecipient` on renderers - for non-extension processes
   (`shouldAddContentKeyRecipients()` returns true when not extension)

WebKit source (`AudioVideoRendererAVFObjC.mm` lines 82-86) declares categories:
```objc
@interface AVSampleBufferDisplayLayer (WebCoreSampleBufferKeySession) <AVContentKeyRecipient>
@end
@interface AVSampleBufferAudioRenderer (WebCoreSampleBufferKeySession) <AVContentKeyRecipient>
@end
```

**Fix**: Added the same ObjC categories in our renderers, call `addContentKeyRecipient`
during renderer initialization, skip per-sample `AVSampleBufferAttachContentKey` when
recipient is registered:

```objc
// In av_sample_buffer_video_renderer.mm constructor:
if (drm_system_) {
  AVContentKeySession* keySession = drm_system_->GetContentKeySession();
  if (keySession) {
    [keySession addContentKeyRecipient:display_layer_];
    content_key_recipient_registered_ = true;
    drm_system_->OnHardwareRecipientAdded();
  }
}
```

Added `GetContentKeySession()` and `OnHardwareRecipientAdded()` to `DrmSystemPlatform`
interface, implemented in `DrmSystemFairplay`.

### Issue 4: Recipient Ordering (addContentKeyRecipient AFTER processContentKeyResponse)

**Symptom**: With `addContentKeyRecipient` fix, -12161 was gone but -12909 appeared.
Log timestamps showed:
- `155147.032` - `processContentKeyResponse` called (key processed)
- `155147.054` - Audio `addContentKeyRecipient` (22ms AFTER key processing)
- `155147.059` - Video `addContentKeyRecipient` (27ms AFTER key processing)

Apple docs: "don't add a recipient to a session that has already begun to process media data"

**Root cause**: The EME handshake completes before the player is created. The flow:
1. JS: `session.update(CKC)` -> `processContentKeyResponse` -> key fulfilled
2. Pipeline resolves -> creates `StarboardRenderer` -> `SbPlayerCreate` -> renderers exist
3. Renderers call `addContentKeyRecipient` (too late)

This is a chicken-and-egg problem: JS needs `session.update()` to resolve for the pipeline
to proceed and create renderers, but recipients must be registered before key fulfillment.

**Fix**: Deferred fulfillment with early callback:
1. In `updateSession`, if no hardware recipients registered yet:
   - Cache the CKC in `_pendingFulfillments`
   - Fire `_sessionUpdatedCallback` and `_keyStatusesCallback` immediately (so JS resolves)
   - Set `callbacksFiredEarly = YES` on session
2. When `onHardwareRecipientAdded` is called (renderer registers):
   - Fulfill all pending CKC responses via `processContentKeyResponse`
3. In `didProvideContentKey`, skip duplicate callbacks if `callbacksFiredEarly`

Result: Recipients registered at `165331.713`, CKC fulfilled at `165331.713` (same call),
correct ordering achieved.

### Issue 5: -12909 kCMFormatDescriptionError_InvalidParameter (Current Blocker)

**Symptom**: With correct recipient ordering, all DRM steps succeed. Multiple audio and
video samples flow through (Decrypt: kSuccess, recipient_registered=1). But AVSBDL reports
decode failure: `Error -11821, underlying_err: NSOSStatusErrorDomain Code=-12909`.

**Investigation so far**:
- Format description is valid: 1280x720, codec=avc1 (0x61766331)
- Sample data starts with SEI NAL (0x06), which is normal for H.264
- `is_annex_b=1` for all samples - AnnexB-to-AVCC conversion happens
- Clear HLS (Phase 1) uses the same conversion and works fine
- Removing `CryptorSubsampleAuxiliaryData` did NOT fix -12909

**Root cause theory**: AnnexB-to-AVCC conversion changes byte positions of encrypted data.
SAMPLE-AES encrypts slice data at specific byte offsets in AnnexB format. Converting 3-byte
start codes (`00 00 01`) to 4-byte AVCC length prefixes shifts the encrypted regions.
Apple's decryptor needs accurate byte boundaries for the encrypted portions.

Additionally, for keyframes, SPS/PPS NALUs are stripped (`bytes_to_skip`) and moved into
the format description. This further shifts the subsample mapping offsets.

The user has added `bytes_to_skip` tracking to `AVSampleBuffer` and adjusted the first
subsample's `clear_byte_count` to account for stripped parameter sets. The
`CryptorSubsampleAuxiliaryData` is now always attached (for both recipient and per-sample
paths) with the adjusted mapping.

**Next steps**:
1. Also account for AnnexB-to-AVCC start code size changes in the subsample mapping
2. Each 3-byte start code becomes a 4-byte length prefix (+1 byte per NAL unit)
3. The subsample mapping needs to be adjusted for every NAL boundary in the clear region

### Architecture Decisions

| Decision | Rationale |
|----------|-----------|
| `addContentKeyRecipient` over `AVSampleBufferAttachContentKey` | Per-sample attach requires private entitlement (extension process only). Cobalt is a standalone app. |
| Deferred fulfillment with early callback | Solves chicken-and-egg: JS needs update resolved to create player, but recipients need player to exist. |
| Strip `skd://` prefix in DRM system | Key ID must match between EME init data (full URI) and TS parser bridge (stripped). Normalization at DRM entry point. |
| Keep `contentIdentifier` separate from `keyId` | Apple's `makeStreamingContentKeyRequestDataForApp:contentIdentifier:` may need the full URI for SPC generation. |

### Files Changed in This Session

| File | Repo | Change |
|------|------|--------|
| `drm_system_fairplay.h` | internal | Added `GetContentKeySession()`, `OnHardwareRecipientAdded()` |
| `drm_system_fairplay.mm` | internal | Key ID stripping, contentIdentifier property, deferred fulfillment, early callbacks, `onHardwareRecipientAdded`, `getContentKeySession` |
| `drm_system_platform.h` | cobalt-github | Added `GetContentKeySession()`, `OnHardwareRecipientAdded()` virtual methods |
| `av_sample_buffer_video_renderer.h` | cobalt-github | Added `content_key_recipient_registered_` member |
| `av_sample_buffer_video_renderer.mm` | cobalt-github | AVContentKeyRecipient category, addContentKeyRecipient, skip per-sample attach, adjusted subsample mapping |
| `av_sample_buffer_audio_renderer.h` | cobalt-github | Added `content_key_recipient_registered_` member |
| `av_sample_buffer_audio_renderer.mm` | cobalt-github | AVContentKeyRecipient category, addContentKeyRecipient, cryptor subsample data |
| `avc_av_video_sample_buffer_builder.mm` | cobalt-github | bytes_to_skip tracking, diagnostic logging |
