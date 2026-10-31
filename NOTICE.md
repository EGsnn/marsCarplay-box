# License and attribution

marsCarplay-box is distributed as a combined work under GPL-3.0-or-later. Its GPL protocol components and local adaptations retain their existing copyright notices. Original permissively licensed portions remain available under their original terms; those notices must be preserved.

| Scope | Origin / license |
|---|---|
| iAP2, pairing, control/RTSP/screen transport | Pinned ESP32 CarPlay Web Relay, DiPlay and retained upstream notices; GPL-3.0-or-later |
| BOX board/display and project integration inherited from the author's workspace | Original Apache-2.0 grant, text in LICENSES/Apache-2.0.txt; base/source fingerprints in references/base-source.json |
| Sub0h264 generic decoder | CraigHutchinson/Sub0h264, MIT; original license and local patch manifest retained |
| Ed25519 implementation | orlp/ed25519, zlib; original license retained |
| ESP-IDF / esp_tinyusb / TinyUSB | External pinned dependencies, their own Apache-2.0/MIT and component notices; retrieved only into ignored local directories |

Historical component THIRD_PARTY_NOTICES files are retained verbatim for attribution. References there to browser/WebRTC dependencies describe upstream provenance; marsCarplay-box does not include or link the web CarPlay applications, ESP Peer, libSRTP or browser preview pages. The root firmware notice describes the actual exported components.

Public-source packaging (2026-10-31): original authentication asset loader replaced by a credential-free stub plus external provider interface; setup/build scripts made independent of the private workspace; diagnostic-only HTTP retained; no identity-bearing binaries or assets distributed. Previous experiment results are historical evidence, not a promise of current compatibility or certification.
