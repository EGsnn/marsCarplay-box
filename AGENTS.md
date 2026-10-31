# marsCarplay-box contributor / AI entry

Read README, PUBLISH_SCOPE, versions/CHANGELOG and the latest version before editing.

- Scope is iPhone USB-C to ATK BOX local H.264 decode/display/touch. No web CarPlay frontend, browser relay or other workspace projects belong here.
- Never add credentials, private certificate/key assets, private media, compiled firmware or Flash images. Default authentication remains an explicit unsupported stub; authorized implementations are external.
- Preserve GPL/MIT/zlib/Apache attributions and fixed third-party source/patch manifests.
- Keep reference-frame dependency order, bounded reception and single decoder/LCD ownership; wait for DMA completion before buffer reuse.
- Keep periodic diagnostics off the LCD. Record build/test/flash/visual acceptance separately in each version document.
- Public setup/build must work without the original workspace. Never flash hardware as part of setup/build or CI.
- Run audit_public.py and applicable host checks before publishing changes. No claim of Apple/MFi certification or stable30/60FPS.
