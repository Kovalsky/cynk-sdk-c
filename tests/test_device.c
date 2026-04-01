#include "internal/cynk_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MAX_TOPIC 256
#define MAX_PAYLOAD 512

struct test_time {
  uint64_t now;
};

struct test_transport {
  char last_publish_topic[MAX_TOPIC];
  char last_publish_payload[MAX_PAYLOAD];
  size_t last_publish_len;
  int publish_count;
  char subscribe_topics[4][MAX_TOPIC];
  int subscribe_count;
};

struct command_capture {
  int called;
  char command[64];
  char request_id[64];
  char slug[64];
  char id[64];
  char params[128];
};

static uint64_t test_now_ms(void *ctx) {
  struct test_time *time = (struct test_time *)ctx;
  return time->now;
}

static int test_now_iso8601(void *ctx, char *buf, size_t cap) {
  const char *ts = "2025-01-01T00:00:00Z";
  size_t len = strlen(ts);
  (void)ctx;
  if (cap <= len) {
    return -1;
  }
  memcpy(buf, ts, len + 1);
  return 0;
}

static int test_publish(void *ctx, const char *topic, const void *payload,
                        size_t len, int qos, int retain) {
  struct test_transport *tx = (struct test_transport *)ctx;
  (void)qos;
  (void)retain;

  tx->publish_count++;
  strncpy(tx->last_publish_topic, topic, sizeof(tx->last_publish_topic) - 1);
  tx->last_publish_topic[sizeof(tx->last_publish_topic) - 1] = '\0';

  if (len >= sizeof(tx->last_publish_payload)) {
    return -1;
  }

  memcpy(tx->last_publish_payload, payload, len);
  tx->last_publish_payload[len] = '\0';
  tx->last_publish_len = len;

  return 0;
}

static int test_subscribe(void *ctx, const char *topic, int qos) {
  struct test_transport *tx = (struct test_transport *)ctx;
  (void)qos;

  if (tx->subscribe_count >= 4) {
    return -1;
  }

  strncpy(tx->subscribe_topics[tx->subscribe_count], topic,
          sizeof(tx->subscribe_topics[0]) - 1);
  tx->subscribe_topics[tx->subscribe_count][sizeof(tx->subscribe_topics[0]) - 1] = '\0';
  tx->subscribe_count++;

  return 0;
}

static void capture_command(void *ctx, const cynk_command *cmd) {
  struct command_capture *cap = (struct command_capture *)ctx;
  cap->called = 1;

  if (cmd->command) {
    strncpy(cap->command, cmd->command, sizeof(cap->command) - 1);
  }
  if (cmd->request_id) {
    strncpy(cap->request_id, cmd->request_id, sizeof(cap->request_id) - 1);
  }
  if (cmd->widget.slug) {
    strncpy(cap->slug, cmd->widget.slug, sizeof(cap->slug) - 1);
  }
  if (cmd->widget.id) {
    strncpy(cap->id, cmd->widget.id, sizeof(cap->id) - 1);
  }
  if (cmd->params_json) {
    strncpy(cap->params, cmd->params_json, sizeof(cap->params) - 1);
  }
}

static cynk_proto *create_test_device(struct test_transport *tx_ctx,
                                      struct test_time *time_ctx) {
  cynk_proto_config cfg;
  cynk_transport tx;

  memset(&cfg, 0, sizeof(cfg));
  cfg.device_id = "dev-1";
  cfg.handshake_timeout_ms = 5000;
  cfg.qos = 1;
  cfg.now_ms = test_now_ms;
  cfg.now_iso8601 = test_now_iso8601;
  cfg.time_ctx = time_ctx;

  memset(&tx, 0, sizeof(tx));
  tx.publish = test_publish;
  tx.subscribe = test_subscribe;
  tx.ctx = tx_ctx;

  return cynk_proto_create(&cfg, &tx);
}

static void test_handshake_and_send(void) {
  struct test_transport tx_ctx;
  struct test_time time_ctx;
  cynk_proto *dev;
  int rc;

  memset(&tx_ctx, 0, sizeof(tx_ctx));
  time_ctx.now = 1000;

  dev = create_test_device(&tx_ctx, &time_ctx);
  assert(dev != NULL);

  rc = cynk_proto_on_connect(dev);
  assert(rc == CYNK_OK);
  assert(tx_ctx.subscribe_count == 2);
  assert(strcmp(tx_ctx.subscribe_topics[0], cynk_proto_status_ack_topic(dev)) == 0);
  assert(strcmp(tx_ctx.subscribe_topics[1], cynk_proto_command_topic_wildcard(dev)) == 0);

  assert(tx_ctx.publish_count == 1);
  assert(strcmp(tx_ctx.last_publish_topic, cynk_proto_status_topic(dev)) == 0);
  assert(strstr(tx_ctx.last_publish_payload, "\"status\":\"online\"") != NULL);

  const char *ack =
    "{\"status\":\"online\",\"user_id\":\"user-1\","
    "\"device_id\":\"dev-1\",\"ts\":\"2025-01-01T00:00:00Z\","
    "\"topics\":{\"telemetry\":\"cynk/v1/user-1/dev-1/telemetry\"}}";

  rc = cynk_proto_handle_message(dev, cynk_proto_status_ack_topic(dev), ack, strlen(ack));
  assert(rc == CYNK_OK);
  assert(cynk_proto_handshake_ready(dev));
  assert(strcmp(cynk_proto_user_id(dev), "user-1") == 0);

  cynk_widget_ref ref = { .id = NULL, .slug = "slider-1" };
  cynk_value value = { .type = CYNK_VALUE_NUMBER, .number = 42.0 };

  rc = cynk_proto_send_value(dev, ref, value);
  assert(rc == CYNK_OK);
  assert(strcmp(tx_ctx.last_publish_topic, "cynk/v1/user-1/dev-1/telemetry") == 0);
  assert(strstr(tx_ctx.last_publish_payload, "\"slug\":\"slider-1\"") != NULL);
  assert(strstr(tx_ctx.last_publish_payload, "\"value\":42") != NULL);

  cynk_proto_destroy(dev);
}

static void test_command_callback(void) {
  struct test_transport tx_ctx;
  struct test_time time_ctx;
  struct command_capture cap;
  cynk_proto *dev;
  int rc;

  memset(&tx_ctx, 0, sizeof(tx_ctx));
  memset(&cap, 0, sizeof(cap));
  time_ctx.now = 1000;

  dev = create_test_device(&tx_ctx, &time_ctx);
  assert(dev != NULL);

  cynk_proto_set_command_cb(dev, capture_command, &cap);

  const char *cmd =
    "{\"command\":\"toggle\",\"request_id\":\"abcd\","
    "\"widget\":{\"slug\":\"relay-1\",\"id\":\"wid-1\"},"
    "\"params\":{\"on\":true}}";

  rc = cynk_proto_handle_message(dev, "cynk/v1/user-1/dev-1/command", cmd, strlen(cmd));
  assert(rc == CYNK_OK);
  assert(cap.called == 1);
  assert(strcmp(cap.command, "toggle") == 0);
  assert(strcmp(cap.request_id, "abcd") == 0);
  assert(strcmp(cap.slug, "relay-1") == 0);
  assert(strcmp(cap.id, "wid-1") == 0);
  assert(strcmp(cap.params, "{\"on\":true}") == 0);

  cynk_proto_destroy(dev);
}

static void test_handshake_timeout(void) {
  struct test_transport tx_ctx;
  struct test_time time_ctx;
  cynk_proto *dev;
  int rc;

  memset(&tx_ctx, 0, sizeof(tx_ctx));
  time_ctx.now = 0;

  dev = create_test_device(&tx_ctx, &time_ctx);
  assert(dev != NULL);

  rc = cynk_proto_on_connect(dev);
  assert(rc == CYNK_OK);

  time_ctx.now = 6000;
  rc = cynk_proto_poll(dev);
  assert(rc == CYNK_ERR_TIMEOUT);

  cynk_proto_destroy(dev);
}

int main(void) {
  test_handshake_and_send();
  test_command_callback();
  test_handshake_timeout();
  printf("All tests passed.\n");
  return 0;
}
