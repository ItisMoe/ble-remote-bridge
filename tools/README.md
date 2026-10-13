# Tools

These are **diagnostic and helper tools, not the bridge firmware**. The bridge itself is in [`../firmware/ble_remote_bridge/`](../firmware/ble_remote_bridge/).

| Tool | Purpose |
|---|---|
| [`remote_sniffer/`](remote_sniffer/remote_sniffer.ino) | Pairs with a Bluetooth remote and prints its services, PnP ID, HID report map and one line per button press. Use it to capture the key codes of a remote, or to check whether another remote is compatible (see [Using another remote](../docs/adapting-to-other-remotes.md)). |
| [`two_host_test/`](two_host_test/two_host_test.ino) | Tests the TV/Chromecast half on its own: the ESP32 shows up as a keyboard called "Relay Test", pairs with a TV and a Chromecast at the same time and presses RIGHT every 3 seconds. The bridge was built to behave like this sketch towards the TV and Chromecast. |
| [`apply_mapping.py`](apply_mapping.py) | Optional, experimental: sends a file of console commands to the bridge over serial. |

You do **not** need the sniffer for the TCL RC902V: the bridge prints the code of every button you press, and [key-codes.md](../docs/key-codes.md) lists them all.

## Using the sniffer

1. Open `remote_sniffer/remote_sniffer.ino`, select *ESP32C3 Dev Module* with *USB CDC On Boot: Enabled* (as for the bridge), upload, and open the Serial Monitor at 115200 baud, line ending Newline.
2. Keep other Bluetooth remotes away (a Chromecast remote would be found too), hold the remote next to the board and put it in pairing mode (RC902V: hold OK + Home for 3 s).
3. It prints the services, the report map and, for every button you press, a line such as `Button pressed: report 3, code 59 00 00 00 00 00` followed by `(released)`. The first two bytes, low byte first, are the code: `59 00` is `0x0059`, `21 02` is `0x0221`.

The sniffer forgets old pairings every time it starts. It works with the stock NimBLE-Arduino 2.5.x library as well as the patched one.

## Using the two-host test

Flash it, remove old "Relay Test" entries on the TV and Chromecast, and pair "Relay Test" on each from *Settings → Remotes & Accessories → Pair accessory*. Every 3 seconds both devices should move one step to the right. It is the quickest way to find out whether a TV or Chromecast accepts the ESP32 at all, before involving the remote. Hold BOOT for 3 seconds to erase its pairings.

## Using `apply_mapping.py`

```
pip install pyserial
python3 tools/apply_mapping.py <port> examples/one-screen-simple.txt
```

Close the Serial Monitor first. `--dry-run` prints the commands without opening the port. The script was tested against a simulated serial device but **not yet on real hardware**; opening the port can restart some ESP32 boards, which is why it waits 2.5 seconds before sending.
