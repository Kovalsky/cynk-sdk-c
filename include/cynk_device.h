#ifndef CYNK_DEVICE_H
#define CYNK_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CYNK_OK 0
#define CYNK_ERR_INVALID_ARG -1
#define CYNK_ERR_NO_HANDSHAKE -2
#define CYNK_ERR_JSON -3
#define CYNK_ERR_TIMEOUT -4
#define CYNK_ERR_NO_MEMORY -5
#define CYNK_ERR_PUBLISH -6
#define CYNK_ERR_SUBSCRIBE -7
#define CYNK_ERR_TIME -8
#define CYNK_ERR_BUFFER -9

#ifndef CYNK_TOPIC_MAX
#define CYNK_TOPIC_MAX 256
#endif

#ifndef CYNK_TS_MAX
#define CYNK_TS_MAX 40
#endif

typedef struct cynk_device cynk_device;

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
} cynk_device_config;

typedef struct {
  const char *id;
  const char *slug;
} cynk_widget_ref;

typedef enum {
  CYNK_VALUE_NUMBER = 0,
  CYNK_VALUE_BOOL = 1,
  CYNK_VALUE_STRING = 2,
  CYNK_VALUE_JSON = 3
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

typedef void (*cynk_command_cb)(void *ctx, const cynk_command *cmd);

typedef void (*cynk_handshake_cb)(void *ctx, const char *user_id);

cynk_device *cynk_device_create(const cynk_device_config *cfg,
                                const cynk_transport *tx);
void cynk_device_destroy(cynk_device *dev);

int cynk_device_on_connect(cynk_device *dev);
int cynk_device_handle_message(cynk_device *dev, const char *topic,
                               const void *payload, size_t len);
int cynk_device_poll(cynk_device *dev);

int cynk_device_handshake_ready(const cynk_device *dev);
const char *cynk_device_user_id(const cynk_device *dev);

void cynk_device_set_command_cb(cynk_device *dev, cynk_command_cb cb, void *ctx);
void cynk_device_set_handshake_cb(cynk_device *dev, cynk_handshake_cb cb, void *ctx);

int cynk_device_send_value(cynk_device *dev, cynk_widget_ref ref, cynk_value value);
int cynk_device_send_raw(cynk_device *dev, const char *telemetry_json, size_t len);

const char *cynk_device_status_topic(const cynk_device *dev);
const char *cynk_device_status_ack_topic(const cynk_device *dev);
const char *cynk_device_command_topic_wildcard(const cynk_device *dev);

int cynk_build_status_payload(const cynk_device *dev, const char *status,
                              char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* CYNK_DEVICE_H */
