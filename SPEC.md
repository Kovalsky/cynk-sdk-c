# Cynk Device SDK (C) — Specification

## Architecture Decision

The SDK uses **built-in MQTT** with a platform abstraction layer rather than a transport-agnostic callback design. This means:

- The public API (`cynk.h`) handles everything: connect, send, receive, disconnect.
- MQTT details are hidden behind an internal `cynk_platform` interface.
- Exactly one platform adapter is linked at build time (no runtime conditionals).
- Users write **three lines of code**, not thirty.

**Rationale:** The original transport-agnostic design (CYN-31) required users to implement 4+ callbacks, manage MQTT connections, configure LWT, and feed messages manually. This was too much friction for the target audience (makers, hobbyists, students). The built-in approach trades flexibility for simplicity — the 95% case (connect to cynk.tech over MQTT/TLS) works out of the box.

**Platform adapters:**

| Adapter | File | MQTT Library | Platforms |
|---|---|---|---|
| Mosquitto | `src/platform/mqtt_mosquitto.c` | libmosquitto | Linux, Mac, Raspberry Pi |
| ESP-IDF | `src/platform/mqtt_esp.c` | esp-mqtt | ESP32, ESP32-S2/S3/C3 |

The protocol-level API (`cynk_protocol.h`) remains available for advanced users who need custom MQTT clients or transport-agnostic integration.

## Goals

- Provide a small, portable C99 SDK for device firmware to publish telemetry and receive commands.
- Three-line integration: `cynk_connect` → `cynk_send` → `cynk_disconnect`.
- Same `cynk.h` API across desktop and embedded platforms.
- Support widget addressing by `slug` or `id` (at least one required).
- Implement the online/offline handshake to discover `user_id` without storing it on the device.
- Keep message building simple: timestamps are set automatically.

## Non-Goals

- No autorespond/ack for every command.
- No UI or CLI in this repository (CLI lives in `cynk-device`).

## MQTT Topics (v1)

- Status (device → backend): `cynk/v1/status/{device_id}`
- Status ack (backend → device): `cynk/v1/status/{device_id}/ack`
- Command (backend → device): `cynk/v1/{user_id}/{device_id}/command`
- Telemetry (device → backend): `cynk/v1/{user_id}/{device_id}/telemetry`

## Payload Shapes (JSON)

Status (device → backend):
```json
{ "status": "online", "device_id": "<id>", "ts": "<iso8601>" }
```

Status (broker LWT, offline):
```json
{ "status": "offline", "device_id": "<id>", "ts": "<iso8601>" }
```

Status ack (backend → device):
```json
{
  "status": "online",
  "user_id": "<user_id>",
  "device_id": "<id>",
  "ts": "<iso8601>",
  "topics": {
    "command": "cynk/v1/<user_id>/<id>/command",
    "telemetry": "cynk/v1/<user_id>/<id>/telemetry"
  }
}
```

Command (backend → device):
```json
{
  "command": "<action>",
  "request_id": "<hex>",
  "widget": { "slug": "<handle>", "id": "<optional>" },
  "params": { ... }
}
```

Telemetry (device → backend):
```json
{
  "ts": "<iso8601>",
  "widgets": [
    { "slug": "<handle>", "id": "<optional>", "payload": { "value": <json_value> } }
  ]
}
```

## Handshake Flow

1. Device configures MQTT with:
   - `client_id = device_id`
   - `username = device_id`
   - `password = device_password`
   - LWT topic `cynk/v1/status/{device_id}` with offline payload.
2. On MQTT connect, the SDK:
   - Subscribes to `cynk/v1/status/{device_id}/ack`.
   - Subscribes to `cynk/v1/+/{device_id}/command` (wildcard for unknown `user_id`).
   - Publishes online status to `cynk/v1/status/{device_id}`.
3. SDK blocks until status ack arrives (default 5 s, configurable).
4. On ack, SDK stores `user_id` and uses the provided telemetry topic.

Notes:
- Device publishes telemetry only after ack is received.
- Commands may arrive any time; the SDK parses and routes them to callbacks.

## Public API (`cynk.h`)

```c
cynk_device *cynk_connect(const char *device_id, const char *password);
int          cynk_send(cynk_device *dev, const char *slug, double value);
int          cynk_send_bool(cynk_device *dev, const char *slug, int value);
int          cynk_send_json(cynk_device *dev, const char *slug, const char *json);
void         cynk_on_command(cynk_device *dev, cynk_command_cb cb, void *ctx);
int          cynk_poll(cynk_device *dev, int timeout_ms);
void         cynk_disconnect(cynk_device *dev);
```

## Protocol-Level API (`cynk_protocol.h`)

For advanced/custom integrations:

```c
cynk_proto *cynk_proto_create(const cynk_proto_config *cfg, const cynk_transport *tx);
void        cynk_proto_destroy(cynk_proto *proto);
int         cynk_proto_on_connect(cynk_proto *proto);
int         cynk_proto_handle_message(cynk_proto *proto, const char *topic,
                                      const void *payload, size_t len);
int         cynk_proto_poll(cynk_proto *proto);
int         cynk_proto_handshake_ready(const cynk_proto *proto);
int         cynk_proto_send_value(cynk_proto *proto, cynk_widget_ref ref, cynk_value val);
```

## Core Types

```c
typedef struct {
  const char *id;     /* optional */
  const char *slug;   /* optional — at least one required */
} cynk_widget_ref;

typedef enum {
  CYNK_VALUE_NUMBER,
  CYNK_VALUE_BOOL,
  CYNK_VALUE_STRING,
  CYNK_VALUE_JSON
} cynk_value_type;

typedef struct {
  cynk_value_type type;
  const char *json;
  double number;
  int boolean;
  const char *string;
} cynk_value;

typedef struct {
  const char *command;
  const char *request_id;
  cynk_widget_ref widget;
  const char *params_json;
} cynk_command;
```

## Error Codes

| Code | Name | Meaning |
|---|---|---|
| 0 | `CYNK_OK` | Success |
| -1 | `CYNK_ERR_INVALID_ARG` | NULL or invalid argument |
| -2 | `CYNK_ERR_NO_HANDSHAKE` | Handshake not completed |
| -3 | `CYNK_ERR_JSON` | JSON parse error |
| -4 | `CYNK_ERR_TIMEOUT` | Handshake timeout |
| -5 | `CYNK_ERR_NO_MEMORY` | Allocation failed |
| -6 | `CYNK_ERR_PUBLISH` | MQTT publish failed |
| -7 | `CYNK_ERR_SUBSCRIBE` | MQTT subscribe failed |
| -8 | `CYNK_ERR_TIME` | Timestamp generation failed |
| -9 | `CYNK_ERR_BUFFER` | Buffer too small |
| -10 | `CYNK_ERR_CONNECT` | Connection failed |

## JSON Handling

- SDK includes jsmn (tiny JSON parser, 128 token limit per message).
- Only required fields are extracted: `status_ack.user_id`, `status_ack.topics.telemetry`, `command.*`.
- Telemetry payloads are constructed by the SDK (no full JSON builder needed).

## Design Notes

- SDK does not validate widget payload types; values are sent as-is.
- No retries inside the SDK; caller decides on reconnect strategy.
- Default QoS is 1; retain is always false.
- If a custom allocator is provided via the protocol API, a matching free must also be provided.
