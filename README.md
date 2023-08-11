# esphome-sesame3

ESPHome external component for Sesame 3 / Sesame 4

## HOW TO USE

See [sesame.yaml](sesame.yaml).

`wifi_ssid`, `wifi_passphrase`, `sesame_pubkey`, `sesame_secret`, `sesame_address` must be set according to your configuration. If you know how to use `secrets.yaml`, use it. If you don't, edit `sesame.yaml` (Remove `!secret ` when replace values).

You can use boards listed in [PlatformIO ESP32 board list](https://registry.platformio.org/platforms/platformio/espressif32/boards). Modify
`esp32:` > `board:` entry.

Then customize `lock:` section to your taste!
