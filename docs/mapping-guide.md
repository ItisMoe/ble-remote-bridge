# Mapping guide

A mapping line says: *when the remote sends this key, send **this** code to the TV and **that** code to the Chromecast.*

```
set <key> <tv> <cc>
```

* `<key>`: the code the remote sends.
* `<tv>` / `<cc>`: the code that device receives, `same` (the key's own code) or `-` (nothing).

Buttons that are **not** in the table are sent to the TV unchanged. Type `map` any time to see the current table, and `defaults` to start again.

## 1. Find a button's code

Press the button. The Serial Monitor prints it:

```
Key 0x0059 TCL key #2 -> TV 0x0059 | Chromecast -
```

Here the key is `0x0059`. The name after it is just a label (`TCL key #2` means "the second key in the capture order", see [key codes](key-codes.md)).

## 2. Recipes

The TCL-specific key codes below belong to the RC902V; with another remote use the codes you see.

### Stop the TV changing volume when you use Left/Right on an HDMI input

On an HDMI input with no menu open, a TV may treat Left/Right as volume. The default table sends the arrows to both devices, so send Left and Right to the Chromecast only:

```
set 0x0044 - same
set 0x0045 - same
```

The TV then ignores Left/Right, so you cannot use them in the TV's own menus. The [Input session](#3-input-session-and-switch-key) below solves that.

### Back and Home for the TV, on two spare keys

Back and Home go to the Chromecast only by default (so the TV does not leave its HDMI input). If you sometimes want them for the TV, give that to two keys you never use:

```
set 0x0059 0x0224 -
set 0x0221 0x0223 -
```

The first line makes TCL key #2 act as **Back on the TV only**; the second makes the mic key act as **Home on the TV only** (the mic key's voice audio does not work through the bridge anyway). `0x0224` is Back and `0x0223` is Home; see [key codes](key-codes.md). The keys lose their original job; `del 0x0059` brings it back.

### Turn a button off

```
set 0x0059 - -
```

Be careful not to switch off Volume and Mute for the TV if nothing else controls the TV's sound.

### Media keys for the Chromecast

```
set 0x0059 - 0x00CD     Play / Pause
set 0x0055 - 0x00B4     Rewind
set 0x005B - 0x00B3     Fast-forward
set 0x0049 - 0x00B5     Next
```

Try an action before assigning it: `send cc 0x00CD`.

### One screen only (everything to the Chromecast)

Send all navigation to the Chromecast and keep only Volume/Mute on the TV. Paste-ready list: [`examples/one-screen-simple.txt`](../examples/one-screen-simple.txt). The TV then only changes volume, and the Chromecast does everything else.

## 3. Input session and switch key

If you want the arrows to go to the Chromecast normally but to the TV when you change inputs, use the **Input session**; for a manual switch use the **switch key**. Both only affect arrows, OK and Back.

1. **Make the arrows and OK Chromecast-only in the normal state:**
   ```
   set 0x0042 - same
   set 0x0043 - same
   set 0x0044 - same
   set 0x0045 - same
   set 0x0041 - same
   ```
2. **Assign the Input key.** Press the remote's Input key and note its code, then:
   ```
   inputkey 0x00XX
   ```
3. **Assign a switch key**, a key you never use. Press it, note the code, then:
   ```
   switchkey 0x00YY
   ```
4. `status` should now list both keys and `Now: normal (table)`.

How it behaves:

| You do | What happens |
|---|---|
| Press **Input** | The Input key goes to the TV (its input menu opens). For the next 5 s the arrows, OK and Back go to the **TV only**. Every arrow/OK/Back press restarts the 5 s. |
| Press **OK** during the session | The session ends at once; arrows return to the Chromecast. |
| Do nothing for 5 s | The session ends by itself. |
| Press the **switch key** | TV mode: arrows, OK and Back go to the TV only. Press it again to leave. |
| Do nothing for 60 s in TV mode | TV mode ends by itself. |

Change the timeouts with `tvsession <seconds>` (1–60) and `tvmode <seconds>` (0 = never). The Input key is sent to the TV with its own code, so this works if the TV acts on that code.

## 4. Making a key open an app (for example Netflix) on the Chromecast

There is **no standard Bluetooth code for "open Netflix"**; each manufacturer uses its own, and the Chromecast may only accept it from its own remote. A procedure that may work:

1. Stop the TV reacting to the key: `set 0x0052 - -` (using that key's code).
2. Try the key's own code on the Chromecast: `send cc 0x0052`. If Netflix opens, `set 0x0052 - same` is enough.
3. If not, capture the code the **Chromecast's own remote** sends for its Netflix button: put that remote in pairing mode next to the board running the [remote sniffer](../tools/remote_sniffer/remote_sniffer.ino). (This unpairs that remote from the Chromecast; pair it again in the Chromecast's settings afterwards.)
4. Test the captured code with `send cc <code>`. If Netflix opens, `set 0x0052 - <code>`.

If step 4 does nothing, the Chromecast most likely accepts that code only from its own remote, and this approach will not work for that key.

## 5. Checking your work

* `map` prints every line.
* Press the button and read the log line: `Key 0x0059 TCL key #2 -> TV 0x0224 | Chromecast -` shows exactly what was sent to each device.
* `del <key>` removes one line; `defaults` restores the factory table.
