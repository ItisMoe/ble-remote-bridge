# Development notes: bugs found, with evidence

How the bridge got to its current form. Each entry says what was **observed**, what the **cause** was and how sure we are of it. Bluetooth addresses in the logs are replaced with placeholders.

## Starting point: two working pre-tests

* [`tools/remote_sniffer`](../tools/remote_sniffer/remote_sniffer.ino): the ESP32-C3 connects to the remote as a Bluetooth host and captures every key. This proved the remote side and produced the report map (`05 0C 09 01 A1 01 85 03 19 00 2A 9C 02 15 00 26 9C 02 95 01 75 10 81 00 C0`: a Consumer Control report, ID 3, one 16-bit usage) and all key codes.
* [`tools/two_host_test`](../tools/two_host_test/two_host_test.ino): the ESP32-C3 acts as a Bluetooth keyboard for the TV **and** the Chromecast at once and presses RIGHT every 3 s. This proved the other side.

The bridge combines the two. Almost every problem below appeared only in the combination.

## The remote "has no button report"

**Observed.** Pairing the remote connected and encrypted the link, then printed `Remote setup failed: no button report found`.

**Cause (found by reading the library).** The code first looked up the report map by its UUID. In NimBLE-Arduino that lookup caches only the one characteristic it found. The next step only ran a full discovery when the cache was empty, so it never ran and the button-report characteristic was never seen. The sniffer always did a full discovery first.

**Fix.** Discover all characteristics of the HID service first, then look things up. *Certainty: high; the log and the library code agree.*

## An endless loop on an all-zero address

**Observed.** After the failure above, the log filled with `Found a remote at 00:00:00:00:00:00 - connecting...` / `Connect failed (rc=3)`, forever.

**Cause.** The first failure put the real remote on a skip list; the scan filter then accepted another nearby device that advertises an all-zero Bluetooth address, which cannot be connected to (`rc=3` is "invalid"), and rescanned immediately.

**Fix.** Never pick an all-zero address; only accept devices that advertise "remote control" or are named `TCL_RC…`; skip a device that fails to connect; wait between rescans. *Certainty: high.*

## Hazard found by reading the code: deleting a pairing while advertising or scanning

NimBLE refuses to delete a pairing that holds an identity key (which devices such as Android TVs typically send) while advertising or scanning is running; it returns "busy" and keeps the pairing. Every deletion in the firmware therefore pauses both first. *This was found by reading the library and was not observed as a failure on hardware.*

## The TV connects but never pairs

**Observed.** With the TV-side Bluetooth profile aligned to the two-host test (same name, IDs, keyboard report, advertising), the TV listed "Relay Test", started "pairing" and then said "couldn't pair". The bridge logged only:

```
Device aa:bb:cc:dd:ee:02 connected on connection 3 - waiting for it to pair/secure...
Dropping an unidentified connection
```

The two-host test, on the same board and TV, logged:

```
Host connected: aa:bb:cc:dd:ee:02 (1 connected)
Sent RIGHT to 1 host(s)
Paired/encrypted with aa:bb:cc:dd:ee:02
```

The difference that remained: the two-host test's `loop()` sends a RIGHT key report to every connected device every 3 s, **before** the device has paired; the bridge sent nothing until a device had paired.

**Fix.** Send the same RIGHT report to a device that has not paired. With it, the TV paired:

```
Device aa:bb:cc:dd:ee:02 connected on connection 1 - waiting for it to pair/secure...
  [conn 1] not paired yet: sent the RIGHT key report of the two-host test (ok)
  [conn 1] pairing/encryption finished: OK (status 0)
>> TV PAIRED: aa:bb:cc:dd:ee:02
```

**Certainty: medium.** The before/after logs agree, but only the presence of the report was changed between them together with some timing, so it is *not proven* that the report itself, and not timing, is what the TV needed.

**Ruled out as the cause** (each was tried and the TV still failed to pair until the report was added):

| Hypothesis | What was tried |
|---|---|
| The extra consumer report in the descriptor | A build with the *exact* keyboard-only descriptor of the two-host test failed in the same way |
| The device name, Vendor/Product IDs, advertising | Made identical to the two-host test. (An early build that used the remote's own IDs also failed, but it differed in other ways too, so the IDs are neither proven guilty nor cleared.) |
| The ESP32's address remembered by the TV from earlier attempts | The two-host test paired with the TV on the same address immediately afterwards |
| Too little pairing storage | Raised from 3 pairings / 8 subscriptions to 8 / 32 (still done, as headroom) and the TV still failed |

## The library crash on connection handle 4

**Observed.** Pairing the Chromecast (the third device):

```
Device aa:bb:cc:dd:ee:03 connected on connection 4 - waiting for it to pair/secure...
Guru Meditation Error: Core  0 panic'ed (Load access fault). Exception was unhandled.
MEPC    : 0x4200f992 ...   MTVAL   : 0x00000858
```

and afterwards the board rebooted in a loop with `BLE assert sch_arb.c 517, param 00000000 00000000` followed by a watchdog reset.

**Cause.** The NimBLE host keeps five tables indexed by the controller's connection handle, sized for handles 0–3, but the controller gave the Chromecast **handle 4**. The write past the end of the tables lands in the memory that holds the connection-update bookkeeping: in the linker map, the `slave_conn` table sits directly in front of the connection-update pool. The crash address decodes to `ble_gap_update_next_exp` (called from `ble_gap_timer`), which walks that list. *Note: the decode was done against a rebuild of the same sources, which is not byte-identical to the binary that crashed, so treat the function name as indicative.*

**Fix.** [`library-patch/`](../library-patch/README.md): 32 slots and bounds checks. **Certainty: high** for the memory overwrite and the crash; **medium** for the reboot loop: the `sch_arb.c` assert is raised inside the closed-source controller, so that it is a consequence of the corrupted bookkeeping is the most likely explanation but was not proven. With the patch the Chromecast connected on connection 4 without a crash, and the assert did not appear again.

**Why the two-host test never crashed.** It never had enough simultaneous connections for the controller to hand out handle 4.

## A failed pairing was saved as the Chromecast

**Observed.** After the crash fix:

```
[conn 4] pairing/encryption finished: FAILED (status 7)

>> CHROMECAST PAIRED: aa:bb:cc:dd:ee:03          <- wrong: the pairing had failed
Chromecast disconnected (reason 0x23D: MIC failure (encryption))
...
[conn 3] pairing/encryption finished: OK (status 0)
A third device paired (aa:bb:cc:dd:ee:04) - rejected: the TV and Chromecast slots are taken
```

**Cause.** The device was accepted because the link reported itself as encrypted, without checking that pairing actually finished. The failed attempt never exchanged the device's real identity, so its temporary address was stored; the successful second attempt then arrived under the real identity address, did not match the Chromecast slot, and was rejected as a "third device" (and its pairing deleted). Status 7 is `BLE_HS_ENOTCONN`: the link dropped during pairing.

**Fix.** A device is only saved when the link is encrypted **and** bonded and no pairing error was recorded; a TV/Chromecast slot without stored keys behind it is cleared at boot. *Certainty: high.*

## Smaller findings

* A message `E (...) NimBLE: No record found for the given address in ble store` appeared when deleting a pairing that did not exist. Harmless; the firmware now checks first.
* An early version measured the "connection age" of an unidentified device from the wrong moment, printing `12 ms` for two minutes. Cosmetic; fixed.
* The key report for an unpaired device was first sent after 1.5 s every 3 s; it is now sent after 3 s and then every 15 s, so that it does not interfere with a device that pairs by itself (the Chromecast).
