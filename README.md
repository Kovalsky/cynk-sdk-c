# Cynk Device SDK (C)

Small, transport-agnostic C99 SDK for Cynk device firmware.

See `SPEC.md` for the full protocol and API design notes.

## Quick Start

1) Implement MQTT connect/loop in your firmware.
2) Provide publish/subscribe/time callbacks to the SDK.
3) Call `cynk_device_on_connect()` after MQTT connects.
4) Feed incoming MQTT messages into `cynk_device_handle_message()`.
5) Use `cynk_device_send_value()` to publish telemetry.

## Minimal Integration

```c
#include "cynk_device.h"

static uint64_t now_ms(void *ctx) {
  /* return monotonic milliseconds */
}

static int now_iso8601(void *ctx, char *buf, size_t cap) {
  /* write ISO8601 UTC timestamp into buf */
  return 0; /* 0 on success */
}

static int mqtt_publish(void *ctx, const char *topic, const void *payload,
                        size_t len, int qos, int retain) {
  /* call your MQTT client */
  return 0;
}

static int mqtt_subscribe(void *ctx, const char *topic, int qos) {
  /* call your MQTT client */
  return 0;
}

static void on_command(void *ctx, const cynk_command *cmd) {
  /* handle command */
}

void setup_device(void) {
  cynk_device_config cfg = {
    .device_id = "device-123",
    .handshake_timeout_ms = 5000,
    .qos = 1,
    .now_ms = now_ms,
    .now_iso8601 = now_iso8601,
    .time_ctx = NULL,
    .alloc = NULL,
    .free = NULL
  };

  cynk_transport tx = {
    .publish = mqtt_publish,
    .subscribe = mqtt_subscribe,
    .ctx = NULL
  };

  cynk_device *dev = cynk_device_create(&cfg, &tx);
  cynk_device_set_command_cb(dev, on_command, NULL);

  /* Configure MQTT LWT using the status topic/payload */
  char lwt_payload[160];
  cynk_build_status_payload(dev, "offline", lwt_payload, sizeof(lwt_payload));
  const char *lwt_topic = cynk_device_status_topic(dev);

  /* Connect MQTT, set LWT to lwt_topic + lwt_payload, then: */
  cynk_device_on_connect(dev);
}

void on_mqtt_message(const char *topic, const void *payload, size_t len) {
  cynk_device_handle_message(dev, topic, payload, len);
}

void send_value(cynk_device *dev) {
  cynk_widget_ref ref = { .slug = "slider-1", .id = NULL };
  cynk_value value = { .type = CYNK_VALUE_NUMBER, .number = 42.0 };
  cynk_device_send_value(dev, ref, value);
}
```

## Notes
- The SDK does not validate widget value types; values are sent as-is.
- `now_ms` and `now_iso8601` are required.
- `cynk_device_send_raw()` publishes a full telemetry JSON string.
- Default QoS is 1; retain is always false.
- If you supply a custom allocator, also supply the matching `free`.

## Troubleshooting

- Handshake timeout: ensure the backend consumer is running and subscribed to `cynk/v1/status/+`.
- TLS CA missing: for the dev broker, regenerate `priv/dev_tls/ca.crt` with `./scripts/gen_dev_mqtt_certs.sh` in the main Cynk repo, or use `--tls-insecure` for local tests.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```
