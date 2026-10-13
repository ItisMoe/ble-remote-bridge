# Command reference

Open the Serial Monitor at **115200 baud** with line ending **Newline**, type a command and press Enter.
Commands are not case-sensitive. Codes are hexadecimal (`0x0045` or `45`, up to four digits). A line can be at most 95 characters.

**Changes apply immediately and are saved in flash.** Pairings are stored separately by the Bluetooth library.

## Everyday commands

| Command | What it does |
|---|---|
| `help` | Prints a short command list. |
| `status` | Shows the remote, TV and Chromecast (paired / connected / receiving), pairing mode, advertising, the Bluetooth IDs, the Input and switch keys with their timeouts, and whether the bridge is in normal mode, a TV session or TV mode. |
| `map` | Prints the mapping table. |
| `log on` / `log off` | Turns the per-button log on or off (default on). Each press prints e.g. `Key 0x0045 Right -> TV 0x0045 \| Chromecast 0x0045`. |

## Mapping

| Command | What it does |
|---|---|
| `set <key> <tv> <cc>` | Sets one button. `<key>` is the code the remote sends. `<tv>` and `<cc>` are what each device receives: a code, `same` (the button's own code), or `-` (nothing; `none` also works). The codes a device receives can go up to 0x029C (the highest code the report format can carry). The table holds up to 64 lines. |
| `del <key>` | Removes a line. The button then goes to the TV unchanged. |
| `defaults` | Restores the factory table (`DEFAULT_MAP` in the sketch). |
| `send <tv\|cc> <code>` | Sends one test press and release to one device, to try an action before you assign it. |

Examples:

```
set 0x0059 0x0224 -       TCL key #2 = Back on the TV only
set 0x0055 - 0x00CD       TCL key #4 = Play/Pause on the Chromecast only
set 0x0044 - same         Left goes to the Chromecast only
set 0x0059 - -            the key does nothing
send cc 0x00CD            test Play/Pause on the Chromecast
```

## Input session and switch key

| Command | What it does |
|---|---|
| `inputkey <code>` / `inputkey off` | Marks the remote's Input key. Pressing it sends its own code to the TV and starts a **TV session**: arrows, OK and Back go to the TV only until OK is pressed or `tvsession` seconds pass without an arrow/OK/Back. It cannot be an arrow, OK or Back, nor the switch key. |
| `switchkey <code>` / `switchkey off` | Marks a key that toggles **TV mode** (arrows, OK, Back to the TV only) and back. The key itself is never sent anywhere. Choose a key you never use. |
| `tvsession <seconds>` | TV-session timeout, 1 to 60, default 5. |
| `tvmode <seconds>` | TV-mode automatic return after this long without any button press, 0 to 3600, default 60. `0` keeps TV mode until the switch key is pressed again. |

## Pairing

| Command | What it does |
|---|---|
| `pair remote` | Looks for the remote for 2 minutes (put the remote in pairing mode and hold it next to the board). Forgets the previous remote first. |
| `pair tv` / `pair cc` | Makes the ESP32 visible for 3 minutes so the TV / Chromecast can pair with it, and pauses the remote until pairing finishes. Forgets the previous TV / Chromecast first. |
| `pair stop` | Cancels pairing. |
| `forget remote\|tv\|cc` | Deletes one pairing. Also remove "Relay Test" on the TV/Chromecast. |
| `swap` | Exchanges which paired device is the TV and which is the Chromecast. |
| `ids tcl\|neutral` | Chooses the Bluetooth Vendor/Product IDs shown to the TV/Chromecast: `neutral` (default, the IDs used by the two-host test) or `tcl` (the remote's own, experimental). Devices read them when they pair, so pair again after changing. |

## Reset

| Action | Effect |
|---|---|
| `factory yes` | Erases **all** pairings and settings, including the mapping, then restarts. |
| Hold the **BOOT** button for 5 seconds | Erases all pairings and the stored device assignments, keeps the mapping and settings, and restarts. |

## Messages you will see

| Message | Meaning |
|---|---|
| `Key 0x.... <name> -> TV ... \| Chromecast ...` | A button press and where it went. `(not sent: ... not connected)` means that device was not connected. |
| `Key ... (TV session started)` / `(TV session)` / `(TV mode)` | The press was routed by the Input key, a TV session or TV mode. |
| `>> TV session ended (...)`, `>> TV MODE (...)`, `>> CHROMECAST MODE (...)` | A TV session or TV mode started or ended. |
| `Remote disconnected (reason 0x216) - it reconnects when you press a button` | Normal: the remote went to sleep or was paused. |
| `Pairing with ... did not complete (status N) - not saved` | A pairing attempt failed; nothing was saved, the device can try again. |
| `A third device paired (...) - rejected` | Both slots are taken; use `forget` first. |
| `WARNING: pairing storage is full` | More pairings than the library can store; use `forget`. |
