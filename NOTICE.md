# Credits and Notices

## Original firmware

The behaviour reconstructed in this project was recovered from the publicly
distributed firmware of **ESP32-BlueJammer** by **@emensta**
(https://github.com/EmenstaNougat/ESP32-BlueJammer). The reconstructed source
was written from static analysis of the published binary, without access to the
upstream application source, and is not a copy of upstream source code.

The OLED bitmaps (`bitmapboot`, `bitmap1` to `bitmap4`) and the banner/ASCII-art
and UI strings belong to the original author. They are **not distributed** with
this repository: `bitmaps.h` and `banners.h` are generated locally by
`tools/gen_assets.py` from a copy of the original BIN that the user supplies.
Do not redistribute those generated headers without permission from the
original author.

## License of this repository

The reconstructed source code is released under the MIT License (see
`LICENSE`). The MIT license covers the code written for this reconstruction.
It does not grant rights to the original author's UI assets: those are not
distributed here and are regenerated locally from a user-supplied BIN.

## Third-party libraries

This sketch is built against:

| Library | License |
|---|---|
| RF24 (TMRh20) | GPLv2 |
| Adafruit SSD1306 | BSD |
| Adafruit GFX Library | BSD |
| Adafruit BusIO | BSD |
| arduino-esp32 core (esp32:esp32 2.0.10) | LGPL-2.1 |

The libraries are not vendored here. If you distribute a compiled firmware
image that links RF24 (GPLv2), that distribution must comply with the GPLv2,
including making the corresponding source available.

## Trademarks

ESP32 is a trademark of Espressif Systems. The names ESP32-BlueJammer and
@emensta are used only to credit the original author and do not imply
affiliation or endorsement.

## Use

Radio jamming in the 2.4 GHz band is illegal in most jurisdictions. This source
is provided for study of firmware reconstruction techniques and must not be
used to transmit.

## Contact

[![Discord Presence](https://lanyard.kyrie25.dev/api/1381277508430856192?animated=false&showDisplayName=false&forceGradient=false&hideNameplate=false&hideDiscrim=false&gradient=FFFF)](https://discord.com/users/1381277508430856192)

