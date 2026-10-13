# NimBLE-Arduino 2.5.1 connection-table patch

The bridge needs a **patched copy of NimBLE-Arduino 2.5.1**. The sketch refuses to compile with the stock library and prints a message pointing here.

## What is wrong in the stock library

The NimBLE host keeps five small tables (`slave_conn`, `g_max_tx_time`, `g_max_rx_time`, `g_max_tx_octets`, `g_max_rx_octets` in `ble_gap.c`) with **one slot per connection, indexed by the controller's connection handle**. They are sized `BLE_MAX_CONNECTIONS + 1`, which is 4 slots (handles 0 to 3) for the 3 connections the bridge uses. But the ESP32-C3 controller handed out **handle 4** while the third device (the Chromecast) was connecting: the library then wrote past the end of the tables, into the memory that holds the connection-update bookkeeping.

On the author's board this showed up as a crash while pairing the Chromecast and then a reboot loop. See [`docs/development-notes.md`](../docs/development-notes.md#the-library-crash-on-connection-handle-4) for the evidence.

## What the patch changes

* the five tables get **32 slots** instead of 4 (`BLE_GAP_CONN_TBL_SZ`);
* every read and write of those tables is **bounds-checked** (`BLE_GAP_CONN_TBL_OK`);
* it adds one marker file, `src/NimBLE_BridgeFix.h`, which the sketch looks for so that it can refuse the stock library.

Only `src/nimble/nimble/host/src/ble_gap.c`, `src/nimble/nimble/host/src/ble_hs_hci_evt.c` and the new marker file are touched. Nothing else in the library changes, and the sketches in `tools/` work with either version.

## How to install

### Option A: the ready-made library (easiest)

Download `NimBLE-Arduino-2.5.1-bridgefix.zip` from the repository's [Releases](https://github.com/ItisMoe/ble-remote-bridge/releases) page. Close the Arduino IDE, delete any existing `NimBLE-Arduino` folder from your Arduino `libraries` folder (for example `Documents/Arduino/libraries`), and unzip the download there so that `libraries/NimBLE-Arduino` exists. Do not update NimBLE-Arduino from the Library Manager afterwards; if it gets updated the sketch will refuse to compile instead of crashing.

### Option B: patch the stock library yourself

1. Install **NimBLE-Arduino 2.5.1** from the Library Manager (exactly this version), or with `arduino-cli lib install NimBLE-Arduino@2.5.1`.
2. Apply the patch from inside the library folder, using either tool:

   ```
   cd <Arduino libraries folder>/NimBLE-Arduino
   git apply --whitespace=nowarn <path to this repo>/library-patch/nimble-arduino-2.5.1-connection-table-fix.patch
   ```
   or
   ```
   cd <Arduino libraries folder>/NimBLE-Arduino
   patch -p1 < <path to this repo>/library-patch/nimble-arduino-2.5.1-connection-table-fix.patch
   ```

   If `git apply` complains that a path is outside the repository, you are inside another git work tree; use `patch -p1`.
3. Check that `src/NimBLE_BridgeFix.h` now exists.

The patch applies to **2.5.1 only**. If it fails (for example because line endings were converted on Windows), use Option A.

## How it was verified

* Applied to a clean copy of NimBLE-Arduino 2.5.1 with `git apply`, the result is byte-for-byte identical to the library the firmware was built and tested with. `patch -p1` accepts it too.
* In the built firmware the five tables have 32 entries each (measured in the linker map).
* With the patched library the Chromecast connected on connection number 4 without a crash (the unpatched library crashed at that point).

## License

NimBLE-Arduino is licensed under the **Apache License 2.0**; the patch is a modification of it and stays under that license. The rest of this repository is MIT.
