# Key codes

Every button on a Bluetooth remote sends a **code**. The bridge's mapping table works with these codes: the code the remote sends (the *key*), and the code each device should receive.

Press a button and the Serial Monitor prints its code (`Key 0x0059 TCL key #2 -> ...`). That is the easiest way to find the code of any button.

## Standard codes you can assign

These are standard *Consumer Control* usages. The last column is what Android makes of them: it was derived from the Linux HID driver (`hid-input.c`) and Android's `Generic.kl` key layout, and individual devices can differ.

| Code | Action | Android key |
|---|---|---|
| `0x0030` | Power | `POWER` |
| `0x0040` | Menu | `MENU` |
| `0x0041` | OK / select | `DPAD_CENTER` |
| `0x0042` | Up | `DPAD_UP` |
| `0x0043` | Down | `DPAD_DOWN` |
| `0x0044` | Left | `DPAD_LEFT` |
| `0x0045` | Right | `DPAD_RIGHT` |
| `0x008D` | Guide | `GUIDE` |
| `0x009C` | Channel + | `CHANNEL_UP` |
| `0x009D` | Channel − | `CHANNEL_DOWN` |
| `0x00B0` | Play | `MEDIA_PLAY` |
| `0x00B3` | Fast-forward | `MEDIA_FAST_FORWARD` |
| `0x00B4` | Rewind | `MEDIA_REWIND` |
| `0x00B5` | Next | `MEDIA_NEXT` |
| `0x00B6` | Previous | `MEDIA_PREVIOUS` |
| `0x00B7` | Stop | `MEDIA_STOP` |
| `0x00CD` | Play / Pause | `MEDIA_PLAY_PAUSE` |
| `0x00E2` | Mute | `VOLUME_MUTE` |
| `0x00E9` | Volume + | `VOLUME_UP` |
| `0x00EA` | Volume − | `VOLUME_DOWN` |
| `0x0221` | Search / Assistant | `SEARCH` |
| `0x0223` | Home | `HOME` |
| `0x0224` | Back | `BACK` |

Notes:

* `0x00B1` (Pause) becomes `BREAK` on Android, not Pause. Use `0x00CD` (Play/Pause) instead.
* `0x0060` has no Android mapping.
* The highest code the bridge can send is `0x029C`.
* Codes outside this standard set (vendor-specific, such as streaming-service shortcuts) are interpreted by each device with its own key layout and may do nothing on a Chromecast.

## The TCL RC902V: every key

Captured from the remote with the [sniffer](../tools/remote_sniffer/remote_sniffer.ino), pressing each button once from top-left to bottom-right. `#n` is the position in that capture; the function of the TCL-specific keys depends on the TV model.

| Code | Button | Default: TV | Default: Chromecast |
|---|---|:-:|:-:|
| `0x00E2` | Mute (capture #1) | `0x00E2` | – |
| `0x0059` | TCL key #2 (capture #2) | `0x0059` | – |
| `0x0221` | Assistant/Search (capture #3) | `0x0221` | – |
| `0x0055` | TCL key #4 (capture #4) | `0x0055` | – |
| `0x0042` | Up (capture #5) | `0x0042` | `0x0042` |
| `0x0044` | Left (capture #6) | `0x0044` | `0x0044` |
| `0x0041` | OK (capture #7) | `0x0041` | `0x0041` |
| `0x0045` | Right (capture #8) | `0x0045` | `0x0045` |
| `0x0043` | Down (capture #9) | `0x0043` | `0x0043` |
| `0x0224` | Back (capture #10) | – | `0x0224` |
| `0x0223` | Home (capture #11) | – | `0x0223` |
| `0x005B` | TCL key #12 (capture #12) | `0x005B` | – |
| `0x0049` | TCL key #13 (capture #13) | `0x0049` | – |
| `0x00E9` | Vol+ (capture #14) | `0x00E9` | – |
| `0x009C` | Ch+ (capture #15) | `0x009C` | – |
| `0x0050` | TCL key #16 (capture #16) | `0x0050` | – |
| `0x00EA` | Vol- (capture #17) | `0x00EA` | – |
| `0x009D` | Ch- (capture #18) | `0x009D` | – |
| `0x003D` | TCL key #19 (capture #19) | `0x003D` | – |
| `0x0052` | TCL key #20 (capture #20) | `0x0052` | – |
| `0x003A` | TCL key #21 (capture #21) | `0x003A` | – |
| `0x0051` | TCL key #22 (capture #22) | `0x0051` | – |
| `0x005F` | TCL key #23 (capture #23) | `0x005F` | – |
| `0x003F` | TCL key #24 (capture #24) | `0x003F` | – |
| `0x0058` | TCL key #25 (capture #25) | `0x0058` | – |
| `0x0057` | TCL key #26 (capture #26) | `0x0057` | – |
| `0x0053` | TCL key #27 (capture #27) | `0x0053` | – |

**Power** is sent by infrared and never reaches the bridge.
