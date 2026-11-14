# Source

`ESP32-BlueJammer/ESP32-BlueJammer.ino` is the frozen reconstructed sketch
(SHA-256 `2cc35b1c86a5765c047b424714649f40979eff7df237da0175963070f269125d`).

The sketch includes `bitmaps.h` and `banners.h`, which are **not shipped**:
they contain UI assets that belong to the original firmware author. Generate
them locally from a copy of the original BIN you own:

```bash
python3 tools/gen_assets.py /path/to/Official-Combo-Channel_BT-BLE-WiFi-RC.ino.bin --out source/ESP32-BlueJammer
```

Then build from the release root:

```bash
bash tools/build.sh /path/to/Official-Combo-Channel_BT-BLE-WiFi-RC.ino.bin
```

See `NOTICE.md` for asset ownership and third-party license terms.
