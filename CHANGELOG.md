# Changelog

## [v0.7.1] 2024-02-06

- Make setup_priority customizable by yaml.
- Update README slightly.

## [v0.7.0] 2023-12-02

- Add history TAG / type sensors.

## [v0.6.0] 2023-11-22

- Embed the battery sensor functionality into the sesame_lock component itself (no need to define the sensor externally).

## [v0.5.0] 2023-11-11

- Add battery reporting functions. See README for usage.

## [v0.4.0] 2023-09-30

- Add `sesame_ble` module. See README for usage.

## [v0.3.0] 2023-09-18

### Major changes

- Add `tag` configuration parameter, specifying tag string recorded in SESAME history (default: "ESPHome").
- Add `lock(const char*)` / `unlock(const char *)` / `open(const char *)` functions (which can be called from lambdas).

## [v0.2.0] 2023-09-14

### Major changes

- SESAME 5 / SESAME 5 PRO / SESAME bot / SESAME bike support
