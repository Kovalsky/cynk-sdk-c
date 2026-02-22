# Cynk Device SDK (C) - Specification

## Goals
- Provide a small, portable C99 SDK for device firmware to publish telemetry and receive commands.
- Keep MQTT client choice up to the user (no hard dependency on a specific MQTT library).
- Support widget addressing by `slug` or `id` (at least one required).
- Implement the online/offline handshake to discover `user_id` without storing it on the device.
- Keep message building simple: default "send" uses only a value; timestamps are set automatically.

## Non-goals
- No autorespond/ack for every command.
- No embedded broker or MQTT client implementation.
- No UI or CLI in this repository (CLI lives in `cynk-device`).

## MQTT Topics (v1)
- Status (device -> backend): `cynk/v1/status/{device_id}`
- Status ack (backend -> device): `cynk/v1/status/{device_id}/ack`
- Command (backend -> device): `cynk/v1/{user_id}/{device_id}/command`
- Telemetry (device -> backend): `cynk/v1/{user_id}/{device_id}/telemetry`

## Payload Shapes (JSON)
Status (device -> backend):
```json
{ "status": "online", "device_id": "<id>", "ts": "<iso8601>" }
```
Status (broker LWT, offline):
```json
{ "status": "offline", "device_id": "<id>", "ts": "<iso8601>" }
```
Status ack (backend -> device):
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
Command (backend -> device):
```json
{
  "command": "<action>",
  "request_id": "<hex>",
  "widget": { "slug": "<handle>", "id": "<optional>" },
  "params": { ... }
}
```
Telemetry (device -> backend):
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
   - `username = device_id` (default)
   - `password = device_password`
   - LWT topic `cynk/v1/status/{device_id}` and LWT payload `{"status":"offline",...}`.
2. On MQTT connect, the device:
   - Subscribes to `cynk/v1/status/{device_id}/ack`.
   - Subscribes to `cynk/v1/+/{device_id}/command` (wildcard for `user_id`).
   - Publishes online status to `cynk/v1/status/{device_id}`.
3. SDK waits for status ack (default 5s, configurable).
4. On ack, SDK stores `user_id` and uses the provided telemetry topic.

Notes:
- Device publishes telemetry only after ack is received.
- Commands may arrive any time; the SDK parses and routes them to callbacks.

## C SDK API (proposed)

### Core Types
```c
typedef enum {
  CYNK_VALUE_NUMBER,
  CYNK_VALUE_BOOL,
  CYNK_VALUE_STRING,
  CYNK_VALUE_JSON
} cynk_value_type;

typedef struct {
  cynk_value_type type;
  const char *json;   /* Used when type == CYNK_VALUE_JSON */
  double number;
  int boolean;
  const char *string;
} cynk_value;

typedef struct {
  const char *id;     /* optional */
  const char *slug;   /* optional */
} cynk_widget_ref;

typedef struct {
  const char *command;
  const char *request_id;
  cynk_widget_ref widget;
  const char *params_json; /* raw params object */
} cynk_command;
```

### Transport Hooks
The SDK calls user-provided publish/subscribe functions; connection is handled by the user.
```c
typedef int (*cynk_publish_fn)(void *ctx, const char *topic, const void *payload, size_t len, int qos, int retain);
typedef int (*cynk_subscribe_fn)(void *ctx, const char *topic, int qos);
typedef uint64_t (*cynk_now_ms_fn)(void *ctx);
typedef int (*cynk_now_iso8601_fn)(void *ctx, char *buf, size_t cap);
```

Time callbacks:
- `now_ms` is used for handshake timeout tracking.
- `now_iso8601` produces UTC timestamps for status/telemetry payloads.

### Device Lifecycle
```c
cynk_device *cynk_device_create(const cynk_device_config *cfg, const cynk_transport *tx);
void cynk_device_destroy(cynk_device *dev);

int cynk_device_on_connect(cynk_device *dev);
int cynk_device_handle_message(cynk_device *dev, const char *topic, const void *payload, size_t len);
int cynk_device_poll(cynk_device *dev); /* checks handshake timeout and maintenance */

int cynk_device_handshake_ready(const cynk_device *dev); /* non-zero when user_id is known */
const char *cynk_device_user_id(const cynk_device *dev);
```

### Device Config (outline)
```c
typedef struct {
  const char *device_id;
  uint32_t handshake_timeout_ms;
  int qos;
  cynk_now_ms_fn now_ms;
  cynk_now_iso8601_fn now_iso8601;
  void *time_ctx;
  cynk_alloc_fn alloc; /* optional */
  cynk_free_fn free;   /* optional */
} cynk_device_config;
```

Notes:
- If `alloc` is provided, `free` must be provided as well.

### Telemetry Helpers
```c
int cynk_device_send_value(cynk_device *dev, cynk_widget_ref ref, cynk_value value);
int cynk_device_send_raw(cynk_device *dev, const char *telemetry_json, size_t len);
```

### Command Handling
```c
typedef void (*cynk_command_cb)(void *ctx, const cynk_command *cmd);
void cynk_device_set_command_cb(cynk_device *dev, cynk_command_cb cb, void *ctx);

typedef void (*cynk_handshake_cb)(void *ctx, const char *user_id);
void cynk_device_set_handshake_cb(cynk_device *dev, cynk_handshake_cb cb, void *ctx);
```

### Topics & Payload Builders
```c
const char *cynk_device_status_topic(const cynk_device *dev);
const char *cynk_device_status_ack_topic(const cynk_device *dev);
const char *cynk_device_command_topic_wildcard(const cynk_device *dev); /* cynk/v1/+/<device_id>/command */
int cynk_build_status_payload(const cynk_device *dev, const char *status, char *buf, size_t cap);
```

## JSON Handling
- SDK will include a tiny JSON parser (e.g., `jsmn`) to extract only the fields needed:
  - `status_ack.user_id`, `status_ack.topics.telemetry`
  - `command.command`, `command.request_id`, `command.widget.id/slug`, `command.params`
- Telemetry payload is constructed by the SDK (no full JSON builder required).

## Value Handling
- SDK does not validate widget payload types. Values are sent as-is.
- Backend is responsible for normalizing known widget payloads and preserving invalid payloads for debugging.

## Errors
- Functions return `0` on success, negative error codes on failure.
- No retries inside the SDK; caller decides on reconnect/publish retry strategy.

## CLI Expectations (for cynk-device)
- Shell mode requires an explicit `send <value>` command per message.
- `send` uses the SDK value helper; `raw` sends a full telemetry JSON as-is.
- CLI prints a summary after each publish (topic, bytes, qos).

## Open Questions
- Should we ship an optional Mosquitto adapter in `examples/` to shorten integration time?
- Default QoS (1) and ack timeout (5s) OK for initial release?
