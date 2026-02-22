#include "cynk_device.h"

#include <stdio.h>
#include <string.h>

static uint64_t example_now_ms(void *ctx) {
  (void)ctx;
  return 0;
}

static int example_now_iso8601(void *ctx, char *buf, size_t cap) {
  const char *ts = "2025-01-01T00:00:00Z";
  size_t len = strlen(ts);
  (void)ctx;
  if (cap <= len) {
    return -1;
  }
  memcpy(buf, ts, len + 1);
  return 0;
}

static int example_publish(void *ctx, const char *topic, const void *payload,
                           size_t len, int qos, int retain) {
  (void)ctx;
  printf("publish qos=%d retain=%d topic=%s payload=%.*s\n", qos, retain, topic,
         (int)len, (const char *)payload);
  return 0;
}

static int example_subscribe(void *ctx, const char *topic, int qos) {
  (void)ctx;
  printf("subscribe qos=%d topic=%s\n", qos, topic);
  return 0;
}

static void example_command(void *ctx, const cynk_command *cmd) {
  (void)ctx;
  printf("command=%s request_id=%s\n", cmd->command,
         cmd->request_id ? cmd->request_id : "-");
}

int main(void) {
  cynk_device_config cfg = {
    .device_id = "device-123",
    .handshake_timeout_ms = 5000,
    .qos = 1,
    .now_ms = example_now_ms,
    .now_iso8601 = example_now_iso8601,
    .time_ctx = NULL,
    .alloc = NULL,
    .free = NULL
  };

  cynk_transport tx = {
    .publish = example_publish,
    .subscribe = example_subscribe,
    .ctx = NULL
  };

  cynk_device *dev = cynk_device_create(&cfg, &tx);
  if (!dev) {
    printf("failed to create device\n");
    return 1;
  }

  cynk_device_set_command_cb(dev, example_command, NULL);

  char lwt_payload[160];
  if (cynk_build_status_payload(dev, "offline", lwt_payload, sizeof(lwt_payload)) != CYNK_OK) {
    printf("failed to build lwt payload\n");
    return 1;
  }

  printf("set LWT topic=%s payload=%s\n", cynk_device_status_topic(dev), lwt_payload);

  if (cynk_device_on_connect(dev) != CYNK_OK) {
    printf("handshake start failed\n");
    return 1;
  }

  const char *ack =
    "{\"status\":\"online\",\"user_id\":\"user-1\","
    "\"device_id\":\"device-123\",\"ts\":\"2025-01-01T00:00:00Z\","
    "\"topics\":{\"telemetry\":\"cynk/v1/user-1/device-123/telemetry\"}}";

  cynk_device_handle_message(dev, cynk_device_status_ack_topic(dev), ack, strlen(ack));

  cynk_widget_ref ref = { .id = NULL, .slug = "slider-1" };
  cynk_value value = { .type = CYNK_VALUE_NUMBER, .number = 10.5 };
  cynk_device_send_value(dev, ref, value);

  cynk_device_destroy(dev);
  return 0;
}
