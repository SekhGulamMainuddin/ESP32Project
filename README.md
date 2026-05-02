# ESP32-S3 BLE LED Controller

This project runs on an ESP32-S3 board and exposes a Bluetooth Low Energy (BLE) server that controls a set of LEDs.

## What this project does

- Initializes 5 LEDs on ESP32-S3 GPIO pins
- Starts a BLE GATT server with read/write and notify characteristics
- Allows BLE clients to send commands that turn LEDs on/off or toggle individual LEDs
- Sends periodic BLE notifications while a client is connected

## Hardware and pin mapping

The LEDs are connected to these GPIOs:

- `GPIO 40` — LED 0
- `GPIO 41` — LED 1
- `GPIO 42` — LED 2
- `GPIO 43` — LED 3 (active-low)
- `GPIO 44` — LED 4 (active-low)

> Note: GPIO 39 is not used because it is input-only on ESP32-S3.

## BLE services and characteristics

The sketch exposes two services:

### Service 1
- UUID: `12345678-1234-1234-1234-123456789012`
- Characteristic 1: `abcdef01-1234-1234-1234-abcdef012345`
  - Read / Write
- Characteristic 2: `abcdef02-1234-1234-1234-abcdef012345`
  - Read / Notify

### Service 2
- UUID: `87654321-4321-4321-4321-210987654321`
- Characteristic 1: `fedcba98-7654-3210-fedc-ba9876543210`
  - Write only

### Service 3
- UUID: `abcdef10-1234-1234-1234-abcdef012345`
- Characteristic 1: `abcdef11-1234-1234-1234-abcdef012345`
  - Write only (blink control)
- Characteristic 2: `abcdef12-1234-1234-1234-abcdef012345`
  - Read only (blink state)

## LED blink feedback

The sketch uses LED blink feedback for BLE operations:

- BLE read operations blink LED 0 (`GPIO 40`)
- BLE write operations blink LED 1 (`GPIO 41`)
- BLE notifications cycle through all LEDs one by one

A separate blink-control service allows enabling or disabling this feedback.

### Blink control commands

Write a single byte to `abcdef11-1234-1234-1234-abcdef012345`:

- `0x00` — disable blink feedback
- `0x01` — enable blink feedback
- `0x02` — toggle blink feedback

Read the blink state from `abcdef12-1234-1234-1234-abcdef012345`:

- returns `enabled` or `disabled`

## BLE command format

Commands are sent as raw byte payloads to the write-only characteristic `fedcba98-7654-3210-fedc-ba9876543210`.

### Supported commands

1. `0x03` — Turn all LEDs ON
   - Updates all LEDs to the ON state

2. `0x04` — Turn all LEDs OFF
   - Updates all LEDs to the OFF state

3. `0x05 [index]` — Toggle an LED
   - `index` is a single byte from `0` to `4`
   - Example: `0x05 0x02` toggles LED 2

4. `0x06 [index][state]` — Set one LED explicitly
   - `index` is a single byte from `0` to `4`
   - `state` is `0x00` for OFF or any non-zero byte for ON
   - Example: `0x06 0x01 0x01` sets LED 1 ON

### Additional BLE behavior

- `0x01` — PING
  - Responds by updating internal status to `pong`
- `0x02 + text` — Rename status
  - Payload after `0x02` becomes part of the status text

## Notification behavior

When a BLE client is connected, the device sends a notification every 2 seconds on the notify characteristic `abcdef02-1234-1234-1234-abcdef012345`.

The notification message contains:

- a tick counter
- the current status string

## Build and upload

This project uses PlatformIO.

1. Open the workspace in VS Code.
2. Set `esp32-s3-devkitm-1` as the active environment.
3. Build with the PlatformIO build command.
4. Upload to the board.

## Serial monitor

Use the serial monitor at `115200` baud to see startup messages, BLE connection events, and command activity.

## Notes

- This project is BLE-only. WiFi code has been removed.
- LEDs 3 and 4 are configured as active-low because of board wiring/LED orientation.
