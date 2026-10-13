# Testing and project status

This is a **beta**. The table shows what has actually been verified on real hardware and what has only been built and compiled. If you test an unverified item, please report the result (see the end of this page).

Hardware used so far: ESP32-C3 SuperMini, TCL RC902V remote, TCL C645 TV (65"), Chromecast with Google TV (4K, first model). Software: esp32 core 3.3.12, NimBLE-Arduino 2.5.1 with the patch.

## Status

| # | Area | Status | Evidence |
|---|---|---|---|
| 1 | Pair the remote; read its report map; capture every key | ✅ verified | Serial logs: `REMOTE PAIRED`, report map and PnP ID match; all 27 Bluetooth keys captured |
| 2 | Pair the TV | ✅ verified on builds 1.6 and 1.7; **re-check on 0.9.0** | Serial log: `[conn 1] pairing/encryption finished: OK`, `TV PAIRED`. Those builds sent the first key report 1.5 s after connecting; 0.9.0 sends it after 3 s (and then every 15 s), which has not been tried on the TV |
| 3 | Pair the Chromecast (third device) | ✅ verified | Paired after a retry (the first attempt hung, the second paired) |
| 4 | No crash or reboot loop with three devices (patched library) | ✅ verified | Chromecast connected on connection 4 without a crash; the unpatched library crashed there |
| 5 | Key presses forwarded to the TV | ✅ verified | Log lines `Key ... -> TV ...`; the TV reacted to the arrow keys |
| 6 | TV reconnects after the ESP32 restarts | ✅ verified | TV showed as connected after re-flashing |
| 7 | Key presses arrive at the **Chromecast** | ⏳ to confirm | |
| 8 | Remote reconnects after it sleeps / after an ESP32 restart | ⏳ to confirm | |
| 9 | Chromecast reconnects after an ESP32 restart | ⏳ to confirm | |
| 10 | TV standby cycle (off for 10+ minutes, then on) | ⏳ to confirm | |
| 11 | TCL-specific keys work on the TV with the default (neutral) Bluetooth IDs | ⏳ to confirm | |
| 12 | Holding a key (volume, arrow) repeats and stops on release | ⏳ to confirm | |
| 13 | Input session and switch key | ⏳ built and compiled only | New in 0.9.0 |
| 14 | `apply_mapping.py` on real hardware | ⏳ tested against a simulated device only | |

Items 1–6 were verified with development builds 1.6 to 1.8. Build 1.8 differs from 0.9.0 only by the Input-session and switch-key feature (item 13) and by comments and log wording; builds 1.6 and 1.7 differ from 1.8 in the pairing-acceptance logic and in the key-report timing noted for item 2.

## Test checklist

Run these in order. Type `status` before each; remote, TV and Chromecast should show as connected.

1. **Status.** All three devices connected.
2. **Mapping.** With the Chromecast's HDMI input on screen:
   * arrows and OK move the Chromecast (and, by default, also the TV);
   * Back and Home act on the Chromecast and the TV stays on the HDMI input;
   * Volume and Mute change the TV.
3. **Holding keys.** Hold Vol+ for 3 s: the volume keeps rising and stops when you release. Hold Right: it keeps scrolling and stops on release.
4. **TCL-specific keys.** Press each one and note what the TV does. (This checks status item 11.)
5. **Remote sleep.** Leave the remote alone until `Remote disconnected` appears, then press Right once. It should reach the devices once, without a delay of several seconds, and the TV should not move twice (twice would mean the remote also sent infrared).
6. **Restart.** Unplug and replug the ESP32. No reboot loop; the TV and Chromecast reconnect by themselves, the remote on its first button press.
7. **Standby.** Turn the TV off with Power for 10 minutes or more, then on again. Everything reconnects, and neither device wakes by itself while the TV is off.
8. **Input session.** After assigning keys as in the [mapping guide](mapping-guide.md#3-input-session-and-switch-key): press Input (the TV's input menu opens; the log says `(TV session started)`), move with the arrows (`(TV session)`), press OK (`TV session ended`), and check that the arrows then control the Chromecast.
9. **Switch key.** Press it (`>> TV MODE`), press it again (`>> CHROMECAST MODE`), and once more wait 60 s to see it return by itself.

## How to report a result

Open an issue with the item number, what you did, what happened, the **full Serial Monitor text** around it, and your board, remote, TV and Chromecast models. Reports of *passing* tests are just as useful as failures: they let the status table be updated.
