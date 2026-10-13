# Using another remote

The firmware was written and tested for the **TCL RC902V**. Other Bluetooth LE remotes may work after changing a few constants, **if** they send keys in the same way. This page tells you how to check that and what to change. It has not been tried with any remote other than the RC902V.

## What the firmware assumes

1. The remote is a Bluetooth LE **HID-over-GATT** device (service `0x1812`).
2. It sends key presses as **notifications on an Input Report characteristic with Report ID 3**.
3. The **first two bytes** of a notification are a **little-endian 16-bit Consumer Control usage code**, and **all zeros mean "released"** (see `handleReport` in the sketch).
4. During pairing it recognises the remote by its advertisement: appearance "remote control" (`0x0180`–`0x01BF`) or a name starting with `TCL_RC` (see `looksLikeRemote`).

If a remote does not follow 2 and 3 (for example it sends keyboard-style reports with several keys at once, or uses several report IDs for different keys), the bridge needs more than a few constants changed.

## Step 1: capture what the remote sends

Flash [`tools/remote_sniffer`](../tools/remote_sniffer/remote_sniffer.ino), put the remote in pairing mode next to the board and press every button once. It prints the services, the PnP ID, the **report map**, the report ID of every input report, and a line per key. For the RC902V:

```
REPORT MAP (25 bytes):
05 0C 09 01 A1 01 85 03 19 00 2A 9C 02 15 00 26 9C 02 95 01 75 10 81 00 C0
Input report id=3 (handle 47) subscribed
Button pressed: report 3, code 59 00 00 00 00 00
     (released)
```

`85 03` in the report map is "Report ID 3"; `05 0C` is the Consumer page; `75 10` / `95 01` is one 16-bit field. The code is the first two bytes, low byte first: `59 00` → `0x0059`.

## Step 2: compare with the RC902V

| Your remote | What to do |
|---|---|
| Same layout (Consumer page, one 16-bit field, report ID 3) | Only the key table and the scan filter need changing (steps 3 and 4). |
| Same layout but a **different report ID** | Also change `REPORT_ID`, and the report ID inside `BRIDGE_REPORT_MAP` (the `0x85, 0x03` of the consumer part). |
| Same layout but a **different largest code** | Change `MAX_CODE` and the usage maximum (`0x2A, 0x9C, 0x02` and `0x26, 0x9C, 0x02`) in `BRIDGE_REPORT_MAP`. |
| A different layout | Needs code changes in `handleReport`, `sendCode` and the report maps. Open an issue if you would like help. |

## Step 3: change the constants

All in `firmware/ble_remote_bridge/ble_remote_bridge.ino`:

| What | Where | Change it to |
|---|---|---|
| Remote report format | `RC902V_REPORT_MAP` | Your remote's report map. Only used to warn at pairing if the format differs. |
| Report ID | `REPORT_ID` | Your remote's report ID. |
| Report sizes | `TV_REPORT_LEN`, `CC_REPORT_LEN` | `TV_REPORT_LEN` is the size of the remote's own report (the TV gets exactly that); `CC_REPORT_LEN` the size the report map declares. |
| Highest code | `MAX_CODE` | Highest usage the report map can carry. |
| Remote recognition | `looksLikeRemote()` | Your remote's advertised appearance or name. |
| Report map shown to the TV/Chromecast | `BRIDGE_REPORT_MAP` | Keep the keyboard part (ID 1) as is; replace the consumer part with your remote's. |
| Force TV/Chromecast to read the new descriptor | `HID_LAYOUT_VERSION` | A number not used before (for example `5`): on the next boot the stored TV/Chromecast pairings are cleared (the log tells you to remove "Relay Test" on both and pair again). |

After changing `BRIDGE_REPORT_MAP`, the TV and Chromecast must be paired again (remove "Relay Test" from both first).

## Step 4: key table and labels

1. Use the sniffer's list to fill `DEFAULT_MAP` (your remote's codes and where each should go), `codeName()` (labels for standard codes) and, if you like, the list of numbered vendor keys in `keyLabel()`.
2. Check each standard code against the [key-code table](key-codes.md); the TV/Chromecast only understand standard codes reliably.
3. Redraw the illustration: `docs/images/make_diagrams.py` reads `DEFAULT_MAP` from the sketch and draws the keys at the positions listed in its `LAYOUT` table. Copy and edit that table for your remote.

You can also skip `DEFAULT_MAP` and build the table live with the `set` command.

## Share your remote

If you get another remote working, please open an issue or a pull request with its report map, PnP ID, key codes and the constants you changed, so that a list of known remotes can be added here.
