# Quest tracking fork

Base: StonkGuy/steamac v1.9.8-eac.1, commit b95961884d814bd81a7bcf1eeb4d12c9e3564431.
Owner: TheSatchel/steamac. Task branch: codex/quest-tracking-stability.

## Contract

- Keep the WiVRn v26.9 wire protocol unchanged. Decode its existing hand and Meta body packets.
- Forward actual 26-joint hand poses through an additive VRL_HAND / SVR_HAND extension. Existing head/controller pose, timing, and graphics messages retain their ABI. Packets remain below the 2048-byte local datagram limit.
- Convert tracked joints to the 31-bone OpenVR skeleton, including parent/model spaces. Report Full only for current valid tracked joints; fall back to controller estimation when tracking is absent or stale.
- Forward only valid body points through VRChat OSC over a loopback-only gvproxy UDP forward. Preserve headset space and publish the head alignment reference. Never manufacture tracked feet from an upper-body packet. Calibration remains a VRChat user action.
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

Implemented on the task branch; installation and real-headset validation remain pending.

Verified so far:
- Native runtime regression suite: 543 checks, zero failures.
- New hand conversion / invalid data / rigid-motion / prediction tests: pass natively and under FEX.
- SteamOS/FEX runtime end-to-end suite: Touch, Index, title-style action reads, standby/reconnect, live-view and half-rate pacing all pass.
- Host protocol, pose history, queue policy, encrypted loopback, OSC packet encoding, TCP/UDP hand/body reception, stale tracking rejection, audio-decorator forwarding and hand-cache freshness tests pass.

Build artifacts and dependency downloads live under ignored `work/` and `guest/vr/build-*`. The app retains the existing bundle identifier for settings/keychain compatibility, but uses the Satchel display name and this fork's update feed. Guest disks and user accounts are not repository assets.

For the local build without Apple's optional Metal compiler, the unchanged AIR/metallib inputs were recovered from the verified installed base binaries. Their source hashes are recorded in `work/prebuilt-metal/provenance.json`; the C/C++ components themselves are rebuilt from the source pins. A normal Xcode/CI build can generate these inputs using the existing upstream build scripts.
