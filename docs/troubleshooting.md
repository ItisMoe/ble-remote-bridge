# Troubleshooting

Always start by typing `status` in the Serial Monitor (115200 baud, line ending Newline). It shows what is paired and connected.

## Building and flashing

| Problem | Cause and fix |
|---|---|
| `#error Install the patched NimBLE-Arduino 2.5.1` | The stock library is installed. Install the patched copy; see [library-patch/README.md](../library-patch/README.md). |
| `static assertion failed: build_opt.h is missing` | `build_opt.h` must sit in the same folder as the `.ino`. Open the `.ino` from `firmware/ble_remote_bridge/` and check that `build_opt.h` appears as a second tab. |
| `#error Use the esp32 board package 3.x` / `Use NimBLE-Arduino 2.5.x` | Wrong board package or library version. Use esp32 **3.3.12** and NimBLE-Arduino **2.5.1** (patched). |
| Nothing appears in the Serial Monitor | *Tools → USB CDC On Boot → Enabled*, baud 115200, line ending Newline. |
| The board is not found / upload fails | Hold the **BOOT** button while plugging in the USB cable, then upload (common for ESP32-C3 boards). |
| A deprecation warning for `startServices()` | Expected. The two-host test calls it, so the bridge does too (it does nothing in this library version). |

## Pairing

| Problem | Cause and fix |
|---|---|
| **The remote is not found** | The bridge only accepts a remote that is close (signal stronger than −75 dBm) and advertises as a remote control or is named `TCL_RC…`. Hold the remote right next to the board, put it in pairing mode (RC902V: hold OK + Home for 3 s) and keep other Bluetooth remotes away. Type `pair remote` to search again. |
| `Remote setup failed: ...` | Put the remote in pairing mode again and retry; the bridge scans again by itself during pairing. |
| **"Relay Test" does not appear** in the TV's or Chromecast's *Pair accessory* list | The device probably still has "Relay Test" stored as a **paired** accessory (for example from an earlier attempt or from the two-host test), and Google TV hides it from the list. Open *Remotes & Accessories*, select "Relay Test" and **unpair/forget** it. Then type `pair tv` or `pair cc` and look again; it can take a few seconds to show up. |
| The device shows "Relay Test" but says **"couldn't pair"** or hangs on "pairing" | Wait a few seconds and try again; on the author's Chromecast the first attempt once hung and the retry paired. If it keeps failing: remove "Relay Test" on that device, type `forget tv` (or `forget cc`), `pair tv` (or `pair cc`) and retry. |
| `Pairing with ... did not complete (status N) - not saved` | The attempt failed before the pairing finished. Nothing was saved; the device can try again. Status 7 means the link dropped during pairing. |
| `A third device paired (...) - rejected` | The TV and Chromecast slots are both taken. Use `forget tv` or `forget cc` first, or `swap` if the two ended up the wrong way round. |
| The TV and Chromecast got swapped | Type `swap`. |
| `WARNING: pairing storage is full` | Use `forget` to remove a pairing you do not need. |
| Everything is confused | Hold **BOOT** for 5 s (erases pairings, keeps the mapping), or `factory yes` (erases everything). Then remove "Relay Test" on the TV and Chromecast and pair again. |

## Using the remote

| Problem | Cause and fix |
|---|---|
| `status` shows the remote as "waiting for it to wake up" | Press any button on the remote; it reconnects when it wakes. The first press after a long sleep can be slow. |
| Keys do not reach a device: `(not sent: ... not connected)` | That device is not connected. Check `status`; a TV or Chromecast normally reconnects by itself shortly after the bridge restarts. |
| **Left/Right change the TV volume** on an HDMI input | The default table sends the arrows to both devices. Send them to the Chromecast only: `set 0x0044 - same` and `set 0x0045 - same`. |
| **Back/Home leave the HDMI input** | By default they go to the Chromecast only. If you mapped them to the TV, undo it with `del <key>` or `defaults`. |
| A key does nothing on the Chromecast | Vendor-specific keys (the TCL-specific keys) are usually not understood by a Chromecast. Map a [standard code](key-codes.md) instead. |
| A key does nothing on the TV | Check the log line for that key: it shows what was sent. TCL-specific keys depend on the TV recognising the code; see [Limitations](../README.md#limitations). |
| Everything happens twice on the TV | The remote might be sending infrared as well as Bluetooth after waking up. Keep the TV's own Bluetooth pairing to the original remote removed, and tell us what you see (see [testing](testing.md)). |
| A button seems stuck or repeats | The bridge releases every held key when the remote disconnects. If it still happens, send the Serial Monitor log. |
| Power does not turn the TV on | Power is infrared and never reaches the bridge, and the bridge cannot wake a TV from standby. |
| Voice search does not work | The microphone audio is not relayed; see [Limitations](../README.md#limitations). |

## Crashes and restarts

| Problem | Cause and fix |
|---|---|
| The ESP32 reboots again and again; the log shows `Guru Meditation Error` or `BLE assert sch_arb.c` | This was the bug in the stock library. The sketch cannot be built without the patched library, so if you see this, check that the patched NimBLE-Arduino is really the one being used (the library marker file `src/NimBLE_BridgeFix.h` must exist) and send the full log. |
| The remote keeps saying "disconnected (reason 0x216)" during pairing | Expected: the remote is paused while a TV/Chromecast is pairing. |

## Still stuck?

Open an issue and include: the **full Serial Monitor text from boot**, the output of `status` and `map`, your remote/TV/Chromecast models, and the board you use.
