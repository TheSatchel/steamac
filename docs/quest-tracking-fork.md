# Quest tracking fork

Base: StonkGuy/steamac v1.9.8-eac.1, commit b95961884d814bd81a7bcf1eeb4d12c9e3564431.
Owner: TheSatchel/steamac. Task branch: codex/quest-tracking-stability.

## Contract

- Keep the WiVRn v26.9 wire protocol unchanged. Decode its existing hand and Meta body packets.
- Forward actual 26-joint hand poses through an additive VRL_HAND / SVR_HAND extension. Existing head/controller pose, timing, and graphics messages retain their ABI. Packets remain below the 2048-byte local datagram limit.
- Convert tracked joints to the 31-bone OpenVR skeleton, including parent/model spaces. Report Full only for current valid tracked joints; fall back to controller estimation when tracking is absent or stale.
- Forward valid body points through additive VRL_BODY / SVR_BODY messages and expose native OpenVR GenericTracker devices. Preserve the same stage space as head and hands. Never manufacture tracked feet from an upper-body packet. OSC remains an explicit optional path; the launcher does not require it. Calibration remains a VRChat user action.
- Publish fresh head/controller poses independently of render pacing. Predict the latest pose to the requested frame time once, with bounded extrapolation. Keep graphics submission timestamps unchanged.
- Expose received packet counts and tracking age. Clear cached hands/body at disconnect; reject non-finite poses and stop publishing stale tracking.

## Acceptance

1. Unit tests exercise valid, inactive, stale, out-of-order, non-finite, left/right and parent/model hand data.
2. OSC tests decode the emitted bundle and verify coordinates, orientations, head reference and missing-leg behavior.
3. Pose tests show updated tracking can be used without issuing a new graphics frame and that prediction is not applied twice.
4. Build the host and x86-64 runtime, test the native bridge, then validate real Quest input and reconnects. Tests alone do not certify in-headset behavior.

## Scope

No EAC bypass, game binary modification, fake full-body measurements, changes to OBS/Windows streaming, or dependency on Wi-Fi. Existing license notices and upstream attribution remain.

## Status

Built and installed locally from the task branch. The guest loaded the expected new runtime hash. Real Quest input packets and ADB USB video streaming have been observed. A 30-second sampling run stayed connected in all 15 samples, with reported head data age 0–5 ms. Visible finger articulation, body OSC calibration and perceived head-follow behavior still require user acceptance.

Verified so far:
- Native runtime regression suite: 543 checks, zero failures.
- New hand conversion / invalid data / rigid-motion / prediction tests: pass natively and under FEX.
- SteamOS/FEX runtime end-to-end suite: Touch, Index, title-style action reads, standby/reconnect, live-view and half-rate pacing all pass.
- Host protocol, pose history, queue policy, encrypted loopback, OSC packet encoding, TCP/UDP hand/body reception, stale tracking rejection, audio-decorator forwarding and hand-cache freshness tests pass.

Build artifacts and dependency downloads live under ignored `work/` and `guest/vr/build-*`. The app retains the existing bundle identifier for settings/keychain compatibility, and window title for the existing OBS capture target; fork provenance is in Info.plist and updates point to this fork. Guest disks and user accounts are not repository assets.

For the local build without Apple's optional Metal compiler, the unchanged AIR/metallib inputs were recovered from the verified installed base binaries. Their source hashes are recorded in `work/prebuilt-metal/provenance.json`; the C/C++ components themselves are rebuilt from the source pins. A normal Xcode/CI build can generate these inputs using the existing upstream build scripts.

## Local build and integration evidence

- The local app build targets macOS 26 (the tested Mac runs macOS 26.6.2). It uses OpenSSL 3.6.3 statically and the LLVM 15.0.7 bottle, with LLVM's unwind library bundled beside the bridge. No Homebrew path is required by the installed app.
- The Sentry SDK is the same checksum-pinned 9.30.0 static artifact, declared directly to avoid downloading unused platform variants and source history. No upstream telemetry endpoint is configured.
- A temporary loopback-only UDP forward was exercised on the active Mac/SteamOS VM: the guest received and echoed the exact probe bytes. The temporary test forward was removed afterwards.
- Real Quest hand/body calibration and perceived head latency are still pending deployment and headset testing.

## Existing guest-agent migration

The Steam VR launch selector is case-sensitive in this installation: use `/VR`. The Rust source now emits that selector. Existing disk images receive a narrow `steam` wrapper only in the progress agent's PATH; it normalizes the VR selector, suppresses a second request while a matching game is running, and debounces startup requests for five seconds. The wrapper is included in repacked layers and can also be installed as a per-user service override without replacing the running guest agent. `scripts/satchel/test-launch-wrapper.py` verifies selector case, duplicate suppression and argument boundaries.

## Native body / hand stability update

The first live trial confirmed improved head latency and real finger motion, but exposed hand jitter and an unavailable VRChat OSC receiver. The next build adds adaptive smoothing only in the hand skeleton's parent-local coordinates, without filtering the HMD/grip poses, and exposes body data as native OpenVR GenericTracker devices. The FEX runtime test discovers the expected four upper-body trackers and no invented feet. Physical calibration and avatar behavior remain to be verified.

### Native tracker protocol

`SVR_CAP_BODY_TRACKERS` gates the additive `SVR_BODY` query. `VRL_BODY` carries at most eight valid poses (hips, chest, feet, knees, elbows) in the same OpenXR/OpenVR stage coordinates as the HMD. The guest exposes them as OpenVR device indices 3–10, class `GenericTracker`, with stable `satchel-body-N` serials. Freshness expires after 200 ms and device activation/deactivation events follow the valid mask. Existing frame and hand message layouts remain unchanged.

The 0.1.1 development build replaced the launcher’s default OSC body path with native trackers. Optional OSC output remains available through its explicit host argument. The user confirmed that head latency improved and that finger tracking works, but reported jitter, loss-related drift and no body following. Quest logs showed `xrLocateBodyJointsFB` failing with `XR_ERROR_INITIALIZATION_FAILED`; restarting the WiVRn client restored all eight body validity bits, but the user still reported no avatar body following. Valid packets alone are not acceptance.

### Tracking loss recovery (0.1.2 development build)

- Pose history records invalid samples, rejects reordered samples from older production times, clears the previous trajectory on loss/reacquisition, and expires validity after 200 ms. Velocity fields are used only when the source marks them valid.
- In hand mode, an inferred wrist without positional tracking does not remain a live controller or full skeleton. Physical controllers retain their existing validity behavior.
- A live headset with no valid body data for three seconds triggers BODY removal from the WiVRn tracking request, followed by reinsertion 500 ms later. WiVRn 26.9 destroys/recreates its body tracker on this transition. Retries are at least ten seconds apart and limited to three until valid data returns; a sleeping headset does not trigger retries.
- Native body device class and discovered identity survive a temporary invalid pose. Their poses become invalid, rather than emitting a device-unplug event that can discard calibration. A real headset disconnect clears the discovered mask.
- Optional `STEAMAC_VR_LOG=1` diagnostics report whether the game reads native body poses and whether hip coordinates change. Logs remain local.

Real avatar body following and loss/reacquisition behavior still require in-headset verification after installing this build.
