# esphome-sesame3

[ESPHome](https://esphome.io/) Smart Lock component for SESAME 5 / SESAME 5 PRO / SESAME bot / SESAME 3 / SESAME 4 / SESAME Bike

## HOW TO USE

See [sesame.yaml](sesame.yaml).

`wifi_ssid`, `wifi_passphrase`, `sesame_pubkey`, `sesame_secret`, `sesame_address` must be set according to your configuration. If you know how to use `secrets.yaml`, use it. If you don't, edit `sesame.yaml` (Remove `!secret ` when replace values).

You can use boards listed in [PlatformIO ESP32 board list](https://registry.platformio.org/platforms/platformio/espressif32/boards). Modify
`esp32:` > `board:` entry.

Then customize `lock:` section to your taste!

## Notes on SESAME bot

SESAME bot supports `lock.open` action in addition to `lock.lock` and `lock.unlock`. `lock.open` performs the same behavior as a smartphone SESAME app.

## Related

* SESAME access library for ESP32 [libsesame3bt](https://github.com/homy-newfs8/libsesame3bt)
* SESAME 5 / SESAME 5 PRO Smart Lock [CANDY HOUSE](https://jp.candyhouse.co/products/sesame5) (SESAME 3 and 4 are End of Sale)