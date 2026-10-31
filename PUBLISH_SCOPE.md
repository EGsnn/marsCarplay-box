# Publication boundary

Published repository: **marsCarplay-box**. This is a new source-only snapshot with a clean initial Git history, exported from the independent `atk-box-carplay` project.

Included: BOX BSP/display/touch, required shared USB/iAP2/CarPlay transport, Sub0h264, bounded queue and diagnostics, credential-free authentication stub/interface, native tests, build scripts, source provenance, licenses and numerical version/performance notes.

Excluded: the original workspace and its Git history; `firmware/carplay_web`, `tools/carplay-live-preview`, `tools/carplay-headunit-test`; web/browser preview/relay applications, web pages/JavaScript/WebRTC frontends, website/backend/Mars system and other device applications; private keys/certificates, hotspot credentials, complete local dependencies, binary firmware/ELF/Flash backups, private media, APK and ZIP archives.

Shared protocol sources necessarily preserve their original authors, license notices and upstream URLs, including publicly named web-relay projects. No private web CarPlay application is copied into this repository. Hardware MAC addresses in historical version documents are omitted.

`code/tools/audit_public.py` checks the source boundary and common credential markers. The public default build has no authentication identity. Future contributors must keep the same boundary.
