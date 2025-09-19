# Changelog

## [v0.23.0] 2025-09-19
- In case of a connection error or unexpected disconnection, battery-related sensors will immediately be issued with an `UNKNOWN` status.
- When a lock becomes "UNKNOWN", the "UNKNOWN" state is issued to history-related sensors.

## [v0.22.1] 2025-09-16
- Bump sesame_ble dependency version aligned to sesame component.

## [v0.22.0] 2025-09-15
- For SESAME OS3 devices, it is no longer necessary to specify the Bluetooth address, you can specify the UUID instead.
- Fix Remote, Bot 2 battery voltage (was doubled).
- SESAME bike 2 supported.
- Support Aug 2025 version of SESAME bot 2 and bike 2

## [v0.21.0] 2025-09-10
- Fix insufficient dependent library declaration in `sesame_ble`

## [v0.20.0] 2025-09-07
- Add `battery_critical` binary_sensor to sesame component
- `is_critical` status of SESAME 5 / PRO is reflected to lock status `JAMMED`

## [v0.19.0] 2025-08-10
- Support ESPHome 2025.7.0 (Arduino ESP32 3.x)
  (fixes #9)

## [v0.18.1] 2025-06-14
- Add ESPHome minimum version validation

## [v0.18.0] 2025-06-08
- Support SESAME Face / PRO
- Support new History TAG format for SESAME May 2025 update

## [v0.17.0] 2025-05-31
- Make `update()` callable from lambda (was mistakenly made private).
- Improved compatibility with [esphome-sesame_server](https://github.com/homy-newfs8/esphome-sesame_server).
  You can now connect to SESAME Touch/Remote devices registered on the server.
- When using dual-role, use [esphome-sesame_server](https://github.com/homy-newfs8/esphome-sesame_server) v0.2.0 or later.

## [v0.16.1] 2025-05-10

- Fix connection instability.

## [v0.16.0] 2025-05-05

- Add `fast_notify` option to `lock` devices.

## [v0.15.0] 2025-04-05

- Use NimBLE-Arduino async connection functionality, connecting operation stability improved
- Incorporate SESAME 4 history retrieval patches (#6)

## [v0.14.1] 2025-03-20

- Fix boot loop on ESP32 S3

## [v0.14.0] 2025-03-01

- SESAME Locks history retrieval sequence changed
- SESAME bot (not Bot2) history and status handling improved
- Properly handle JAM detection timeout (SESAME 3/4)
- Documentation modification to README History type table
  - DRIVE_LOCKED/DRIVE_UNLOCKED was observed on SESAME 4
  - Added BLE_CLICK

## [v0.13.0] 2024-12-30

- Auto import libsesamebt3 library

## [v0.12.0] 2024-12-30

- Bump NimBLE-Arduino version to 2.1.2
- Bump libsesame3bt version to 0.21.0
- Change timeout setting (`connection_timeout`, `unknown_state_timeout`) precision to milliseconds (was seconds)

## [v0.11.0] 2024-09-14

- Support SESAME Bot 2.
- Add bot keyword for Bot devices.

## [v0.10.0] 2024-04-29

- Big YAML schema change (see README).
- Support SESAME Touch / Touch PRO / Bike 2 / Open Sensor (Tested only on Touch. Please report other devices).
- Add option
  - always_connect
  - update_interval

## [v0.9.0] 2024-04-04

- Update libsesame3bt library to 0.16.0
- Added some options (see README)
	- timeout
	- connection_sensor
	- unknown_state_alternative
	- unknown_state_timeout
	- connect_retry_limit
- action `lock`, `unlock`, `open` can accept `string` directory (no need to `c_str()`).

## [v0.8.0] 2024-03-03

- Start BLE connection according to `setup_priority` (was start connecton too early).
- Strictly check conflicts with other BLE modules.

## [v0.7.2] 2024-02-07

- Enhance YAML validation.

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
