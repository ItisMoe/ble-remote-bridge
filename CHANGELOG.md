# Changelog

Format based on [Keep a Changelog](https://keepachangelog.com/). Versions before 0.9.0 were internal development builds, listed at the bottom for reference.

## [0.9.0] - first public release (beta)

First public version, formerly development build 1.9.

### Added
- **Input session**: the remote's Input key (`inputkey`) sends its code to the TV and hands the arrows, OK and Back to the TV until OK is pressed or `tvsession` seconds (default 5) pass without an arrow/OK/Back.
- **Switch key** (`switchkey`): toggles TV mode (arrows, OK, Back to the TV only); returns by itself after `tvmode` seconds (default 60, 0 = never).
- Settings `tvsession`, `tvmode`, `inputkey`, `switchkey` shown in `status` and stored in flash.
- Repository documentation, diagrams, example mapping files, optional `tools/apply_mapping.py`.

### Changed
- Firmware renamed to **BLE Remote Bridge**; comments and log messages reworded for a public audience. No code changes compared with development build 1.9.

### Known limitations
See the README. Notably: voice is not relayed, Power is infrared, and several behaviours are not yet verified on hardware (see `docs/testing.md`).

## Development builds (before the first release)

| Build | Changes |
|---|---|
| 1.0 | First bridge: remote as BLE central, TV and Chromecast as BLE peripheral, mapping table, serial console. |
| 1.1 | Fixed "no button report found" (full service discovery before lookups); never select an all-zero address; skip devices that fail; safe pairing deletion; restart a paused scan. |
| 1.2 | Log every connect/disconnect with a reason; `ids` command to switch Bluetooth IDs. |
| 1.3 | TV-side profile aligned with the two-host test; `pair tv/cc` pauses the remote; `swap`; advertise like the test. |
| 1.4 | TV-side setup calls identical to the two-host test, same name; `KEYBOARD_ONLY_LIKE_TEST` diagnostic switch. |
| 1.5 | Raised pairing/subscription storage via `build_opt.h` (8 / 32). Correct remote model (RC902V). |
| 1.6 | **Fix for the TV never pairing:** send the two-host test's RIGHT key report to an unpaired device; log each pairing step. |
| 1.7 | **Library patch** for the crash on connection handle 4; do not listen for the remote while the TV/Chromecast connect or pair. |
| 1.8 | Save a TV/Chromecast only after pairing finished and keys were stored; clear slots without keys at boot; key report after 3 s then every 15 s. |
| 1.9 | Input session and switch key (became 0.9.0). |
