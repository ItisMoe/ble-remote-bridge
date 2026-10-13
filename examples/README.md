# Example mapping files

Each `.txt` file is a list of console commands, one per line. They use the key codes of the **TCL RC902V**; with another remote, use the codes you see when you press its buttons.

| File | What it does |
|---|---|
| [`one-screen-simple.txt`](one-screen-simple.txt) | Everything that navigates goes to the Chromecast, only Volume/Mute stay on the TV, all other keys are switched off. For people who should not have to think about which device they are controlling. |
| [`arrows-chromecast-only.txt`](arrows-chromecast-only.txt) | Arrows and OK go to the Chromecast only. The first step for the [Input session](../docs/mapping-guide.md#3-input-session-and-switch-key). |
| [`back-home-on-spare-keys.txt`](back-home-on-spare-keys.txt) | TCL key #2 becomes Back and the mic key becomes Home, both for the TV only. |
| [`media-keys-chromecast.txt`](media-keys-chromecast.txt) | Four TCL keys become Play/Pause, Rewind, Fast-forward and Next on the Chromecast. |

## How to use a file

The Arduino Serial Monitor sends one line at a time, so either:

* **paste/type the lines one by one**, or
* use the optional helper, which sends a whole file over the serial port (close the Serial Monitor first):

  ```
  pip install pyserial
  python3 tools/apply_mapping.py <port> examples/one-screen-simple.txt
  ```

  (`<port>` is for example `COM5` on Windows or `/dev/ttyACM0` on Linux. See [tools/README.md](../tools/README.md).)

Afterwards type `map` to check the result. `defaults` restores the factory table.
