#ifndef CYNK_PROTOCOL_H
#define CYNK_PROTOCOL_H

/*
 * Internal protocol module — handles MQTT topic routing, handshake state
 * machine, JSON parsing/building, and telemetry construction.
 *
 * NOT a public header. Platform adapters and cynk.c use this internally.
 * Users include <cynk.h> instead.
 */

#include "cynk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CYNK_TOPIC_MAX
#define CYNK_TOPIC_MAX 256
#endif

#ifndef CYNK_TS_MAX
#define CYNK_TS_MAX 40
#endif

typedef struct cynk_proto cynk_proto;

typedef void *(*cynk_alloc_fn)(size_t size);
typedef void (*cynk_free_fn)(void *ptr);

typedef int (*cynk_publish_fn)(void *ctx, const char *topic, const void *payload,
                               size_t len, int qos, int retain);
typedef int (*cynk_subscribe_fn)(void *ctx, const char *topic, int qos);

typedef uint64_t (*cynk_now_ms_fn)(void *ctx);
typedef int (*cynk_now_iso8601_fn)(void *ctx, char *buf, size_t cap);

typedef struct {
  cynk_publish_fn publish;
  cynk_subscribe_fn subscribe;
  void *ctx;
} cynk_transport;

typedef struct {
  const char *device_id;
  uint32_t handshake_timeout_ms;
  int qos;
  cynk_now_ms_fn now_ms;
  cynk_now_iso8601_fn now_iso8601;
  void *time_ctx;
  cynk_alloc_fn alloc;
  cynk_free_fn free;
} cynk_proto_config;

typedef void (*cynk_handshake_cb)(void *ctx, const char *user_id);

cynk_proto *cynk_proto_create(const cynk_proto_config *cfg,
                              const cynk_transport *tx);
void cynk_proto_destroy(cynk_proto *dev);

int cynk_proto_on_connect(cynk_proto *dev);
int cynk_proto_handle_message(cynk_proto *dev, const char *topic,
                              const void *payload, size_t len);
int cynk_proto_poll(cynk_proto *dev);

int cynk_proto_handshake_ready(const cynk_proto *dev);
const char *cynk_proto_user_id(const cynk_proto *dev);

void cynk_proto_set_command_cb(cynk_proto *dev, cynk_command_cb cb, void *ctx);
void cynk_proto_set_handshake_cb(cynk_proto *dev, cynk_handshake_cb cb, void *ctx);

int cynk_proto_send_value(cynk_proto *dev, cynk_widget_ref ref, cynk_value value);
int cynk_proto_send_raw(cynk_proto *dev, const char *telemetry_json, size_t len);

const char *cynk_proto_status_topic(const cynk_proto *dev);
const char *cynk_proto_status_ack_topic(const cynk_proto *dev);
const char *cynk_proto_command_topic_wildcard(const cynk_proto *dev);

int cynk_proto_build_status_payload(const cynk_proto *dev, const char *status,
                                    char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* CYNK_PROTOCOL_H */
