# Standalone FairPlay FPS HLS Test Resource

This directory is a self-contained copy of WebKit's `fps-hls.html` FairPlay HLS layout test and the local CKC stub server it uses.

Source assets are copied from WebKit:

- `LayoutTests/http/tests/media/fairplay/fps-hls.html`
- `LayoutTests/http/tests/media/fairplay/eme2016.js`
- `LayoutTests/http/tests/media/fairplay/support.js`
- `LayoutTests/http/tests/media/fairplay/content/prog_index.m3u8`
- `LayoutTests/http/tests/media/fairplay/content/main.ts`
- `LayoutTests/http/tests/media/fairplay/resources/cert.der`
- `LayoutTests/http/tests/media/fairplay/resources/index.py`
- `LayoutTests/http/tests/media/fairplay/resources/keyserver/`
- `LayoutTests/media/video-test.js`

Upstream reference:

- https://github.com/WebKit/WebKit/tree/main/LayoutTests/http/tests/media/fairplay
- https://github.com/WebKit/WebKit/blob/main/LayoutTests/media/video-test.js

## Setup

Use Python 3.9 on macOS. Python 3.14 fails on the copied WebKit keyserver dataclasses.

```bash
cd /Users/abhijeet/code/cobalt-github/src/fairplay-fps-hls-test-resource
/usr/bin/python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip setuptools wheel
python -m pip install -r requirements.txt
```

## Run

```bash
cd /Users/abhijeet/code/cobalt-github/src/fairplay-fps-hls-test-resource
source .venv/bin/activate
python serve_fps_hls.py --host 0.0.0.0 --port 8000
```

Open the test page:

```text
http://192.168.1.3:8000/media/fairplay/fps-hls.html
```

For Cobalt device runs, use the same URL:

```bash
cd /Users/abhijeet/code/cobalt-github/src
./deploy_and_run_cobalt_device.sh \
  --bundle-id 'abhijeet.tvos.org.chromium.chrome.unittests.dev' \
  --device 'Abhijeet-TVOS' \
  --url 'http://192.168.1.3:8000/media/fairplay/fps-hls.html'
```

## Smoke Tests

Static page:

```bash
curl -I --max-time 5 http://127.0.0.1:8000/media/fairplay/fps-hls.html
```

HLS playlist:

```bash
curl -I --max-time 5 http://127.0.0.1:8000/media/fairplay/content/prog_index.m3u8
```

Range request:

```bash
curl -I --max-time 5 -H 'Range: bytes=0-99' http://127.0.0.1:8000/media/fairplay/content/main.ts
```

CKC endpoint with intentionally invalid SPC:

```bash
curl -i --max-time 10 \
  -H 'Content-Type: application/json' \
  --data '{"fairplay-streaming-request":{"version":1,"streaming-keys":[{"id":1,"uri":"skd://twelve","spc":"AAAA"}]}}' \
  http://127.0.0.1:8000/media/fairplay/resources/index.py
```

Expected result for the fake-SPC probe:

```text
HTTP/1.0 400 Bad Request
Bad request
```

That `400` means the copied keyserver imported correctly and reached SPC parsing. A `500` means the Python environment is still wrong.

## Notes

`serve_fps_hls.py` sets `DISABLE_WEBKITCOREPY_AUTOINSTALLER=1` for the CKC subprocess. This is required because WebKit's `webkitcorepy` autoinstall hook otherwise intercepts `Crypto` imports and tries to install `pycryptodome-3.10.1` into WebKit's autoinstalled directory.

The copied WebKit code is used only as a local test fixture. Keep source references intact when refreshing files from upstream.
