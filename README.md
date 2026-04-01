# Cynk Device SDK (C)

Connect your device to [cynk.tech](https://cynk.tech) in three lines of C:

```c
cynk_device *dev = cynk_connect("my_device", "my_password");
cynk_send(dev, "temperature", 23.5);
cynk_disconnect(dev);
```

Supports **Linux / Mac / Raspberry Pi** (via libmosquitto) and **ESP32** (via esp-mqtt). Same `cynk.h` API on every platform.

## Quick Start — Desktop (Linux / Mac / RPi)

### Prerequisites

```bash
# Ubuntu / Debian / Raspberry Pi OS
sudo apt-get install -y libmosquitto-dev cmake

# macOS
brew install mosquitto cmake
```

### Build & Run

```bash
git clone https://github.com/cynktech/cynk-sdk-c.git
cd cynk-sdk-c
cmake -B build && cmake --build build
./build/cynk_quick_start <device_id> <password>
```

Get your `device_id` and `password` from the cynk.tech dashboard (Settings → Devices).

### Single-File Build (RPi / no CMake)

For projects without a build system, use the amalgamated build:

```bash
./scripts/amalgamate.sh
gcc your_app.c dist/cynk_amalgamation.c -lmosquitto -lssl -lcrypto -o your_app
```

This bundles the entire SDK into two files: `dist/cynk.h` and `dist/cynk_amalgamation.c`.

## Quick Start — ESP32

### Prerequisites

- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)

### Setup

Clone the SDK into your project's `components/` directory:

```bash
cd your_project
git clone https://github.com/cynktech/cynk-sdk-c.git components/cynk-sdk-c
```

Or add it as a git submodule:

```bash
git submodule add https://github.com/cynktech/cynk-sdk-c.git components/cynk-sdk-c
```

The SDK auto-registers as an ESP-IDF component when `ESP_PLATFORM` is detected.

### Example

```c
#include "cynk.h"

void app_main(void) {
    /* ... Wi-Fi + SNTP init ... */

    cynk_device *dev = cynk_connect("my_device", "my_password");
    while (1) {
        cynk_send(dev, "temperature", read_sensor());
        cynk_poll(dev, 5000);
    }
}
```

See [`examples/esp32/`](examples/esp32/) for a complete working example with Wi-Fi and SNTP setup.

### Build & Flash

```bash
cd examples/esp32
# Edit main/main.c with your Wi-Fi and device credentials
idf.py build flash monitor
```

## API Reference

| Function | Description |
|---|---|
| `cynk_connect(device_id, password)` | Connect to cynk.tech. Blocks until handshake completes (~5 s timeout). Returns `NULL` on failure. |
| `cynk_send(dev, slug, value)` | Send a numeric value to a widget by slug. |
| `cynk_send_bool(dev, slug, value)` | Send a boolean (0/1) to a widget. |
| `cynk_send_json(dev, slug, json)` | Send a raw JSON string to a widget. |
| `cynk_on_command(dev, callback, ctx)` | Register a callback for incoming dashboard commands. |
| `cynk_poll(dev, timeout_ms)` | Process network events. Call periodically to receive commands. |
| `cynk_disconnect(dev)` | Disconnect and free all resources. |

All `cynk_send*` functions return `CYNK_OK` (0) on success, negative error code on failure.

## Receiving Commands

```c
static void on_command(void *ctx, const cynk_command *cmd) {
    printf("Command: %s, widget: %s\n", cmd->command, cmd->widget.slug);
}

cynk_on_command(dev, on_command, NULL);
while (1) {
    cynk_poll(dev, 1000);  /* dispatch incoming commands */
}
```

## Architecture

```
cynk_connect / cynk_send / cynk_disconnect   ← Public API (cynk.h)
        │
   cynk_proto_*                               ← Protocol (handshake, JSON, topics)
        │
   cynk_platform_*                            ← Platform interface (abstract)
        │
   ┌────┴─────┐
mqtt_mosquitto  mqtt_esp                      ← Adapters (link-time binding)
```

The platform adapter is selected at **link time** — exactly one `.c` file is compiled in. The public API is identical across all platforms.

## Protocol-Level API

For advanced use cases (custom MQTT clients, unit testing), the internal protocol API is available:

```c
#include "internal/cynk_protocol.h"
```

See [`examples/basic_device.c`](examples/basic_device.c) for a complete protocol-level example.

## Build & Test (Development)

```bash
cmake -B build && cmake --build build
ctest --test-dir build
```

## Troubleshooting

- **Handshake timeout**: Ensure the backend consumer is running and subscribed to `cynk/v1/status/+`.
- **Desktop TLS CA missing**: Install `ca-certificates`. For the dev broker, use `./scripts/gen_dev_mqtt_certs.sh` in the main Cynk repo.
- **ESP32 time not syncing**: SNTP requires a working internet connection. Check Wi-Fi connectivity first.
- **ESP32 stack overflow**: Increase `CONFIG_ESP_MAIN_TASK_STACK_SIZE` in `menuconfig` (8192 recommended).

## Specification

See [`SPEC.md`](SPEC.md) for the full MQTT protocol contract, payload shapes, and architecture decisions.
