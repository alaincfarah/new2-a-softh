# SoftphonePJSIP (Android + PJSIP C backend)

Android softphone scaffolding using a C-language backend (PJSIP `pjsua`) to register/call against **FreePBX** over an OS-level **OpenVPN** connection.

## Features implemented in this scaffold
- **SIP registration + calling** via PJSIP (`pjsua`) from native code.
- **Call hold / unhold**.
- **Blind transfer** (`REFER` / `xfer`).
- **Warm transfer** (consult call + `xfer-replaces`, best-effort).
- **Call recording** to WAV (app private external storage).
- **Critical behavior**: on incoming call, **open a URL based on caller CNAME** (either treat CNAME as URL if it starts with `http(s)://` or expand a `{cname}` template).

## Critical dependencies / assumptions

### PJSIP dependency (YOU must provide pjproject)
This repo does **not** vendor PJSIP.

Place the PJSIP source tree at:

- `app/src/main/cpp/pjproject`

The native build expects **prebuilt** PJSIP static libs under:

- `app/src/main/cpp/pjproject/lib`

> If your build outputs libs to a different directory or uses different naming, update `app/src/main/cpp/CMakeLists.txt` (`PJSIP_LIB_DIR` and imported library filenames).

### VPN dependency
The app assumes the Android device is already connected to the FreePBX network via **OpenVPN at OS level**. The app does not implement a VPN client.

## Codec support (G.729 / uLaw / aLaw)
- **uLaw**: `PCMU/8000` (supported by PJSIP)
- **aLaw**: `PCMA/8000` (supported by PJSIP)
- **G.729**: PJSIP does **not** ship a G.729 codec implementation by default (licensing). You must integrate a compatible G.729 codec for PJSIP (and respect licensing) and ensure the built codec is included in your Android build. The native code sets G.729 priority if present.

## How to build (high level)
1. Put your `pjproject/` at `app/src/main/cpp/pjproject`.
2. Build pjproject for Android ABIs you want (e.g. `arm64-v8a`, `armeabi-v7a`) and make the resulting static libs available under `app/src/main/cpp/pjproject/lib`.
3. Open this folder in Android Studio and build `app`.

## Incoming-call URL behavior
In the main screen, set **Incoming URL template** like:

- `https://your.internal/app?cname={cname}`

On incoming call, the service will:
- extract the caller display-name (best effort) from `remote_info`
- URL-encode it
- replace `{cname}` in the template
- launch `Intent.ACTION_VIEW` with that URL

