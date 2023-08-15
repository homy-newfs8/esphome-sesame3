# esphome-sesame3

[ESPHome](https://esphome.io/) Smart Lock component for SESAME 3 / SESAME 4

## HOW TO USE

See [sesame.yaml](sesame.yaml).

`wifi_ssid`, `wifi_passphrase`, `sesame_pubkey`, `sesame_secret`, `sesame_address` must be set according to your configuration. If you know how to use `secrets.yaml`, use it. If you don't, edit `sesame.yaml` (Remove `!secret ` when replace values).

You can use boards listed in [PlatformIO ESP32 board list](https://registry.platformio.org/platforms/platformio/espressif32/boards). Modify
`esp32:` > `board:` entry.

Then customize `lock:` section to your taste!

## Related

* SESAME access library [libsesame3bt](https://github.com/homy-newfs8/libsesame3bt)
* SESAME 3 / SESAME 4 Smart Lock [CANDY HOUSE](https://jp.candyhouse.co/products/sesame5) (SESAME 3 and 4 are End of Sale)
