# Exported firmware provenance

This BOX direct-display source includes required GPL-3.0-or-later iAP2, pairing, RTSP/control/screen, timing and HID components adapted from https://github.com/xujingguo58/esp32-carplay-web-relay at `6ab9eb9d46b8f91ee7cc5574e5b2b4693405e71c`, with inherited DiPlay attribution. Original component COPYING and THIRD_PARTY_NOTICES files remain. They describe broader upstream provenance, not a claim that upstream browser applications or all their dependencies are bundled here.

The BOX BSP/integration was exported from the author's Apache-2.0 workspace; base commit and source fingerprints are recorded in references/base-source.json. The original Apache-2.0 text is preserved in LICENSES/Apache-2.0.txt. Modified combined firmware is distributed under GPL-3.0-or-later; third-party permissive grants remain.

Sub0h264 at `15421eeade48774929bc0221ea0418a25462c222` is MIT. Its original license, cooperative/profile hooks and bitstream modifications are retained and recorded in references/sub0h264-source.json. Ed25519 comes from orlp/ed25519 `b1f19fab4aebe607805620d25a5e42566ce46a0e`, zlib license, preserved under components/ed25519.

ESP-IDF v6.1, esp-usb and TinyUSB are external public dependencies pinned in upstream-pins.json. Their own license files remain in local dependency checkouts after setup. main/probe_network.c retains its ESP-IDF example attribution (Unlicense OR CC0-1.0).

Identification/rejection structures were compared with catplay-labs/catplay `bc6edae49554f2d0160b18ed18fc7b7376353115` and DiPlay `4487c689bfdd3baab12de67a993a671b67939eb7`. Post-RECORD requestUI/changeModes shapes were compared with lvalen91/carplayd `4eb254499aa00f456aea91d00755cf150c7d18a4`. Local command/plist glue and the RAM bounded queue are retained with applicable notices.

Public packaging replaces the experimental asset-loading signer with an unsupported stub and an external authentication-provider hook. No authentication identity, APK, private key/certificate or binary firmware is distributed. Historical asset acceptance is not Apple certification. This source repository includes no web CarPlay frontend, browser preview/relay application, ESP Peer or libSRTP dependency.
