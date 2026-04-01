#ifndef CYNK_PLATFORM_H
#define CYNK_PLATFORM_H

/*
 * Internal platform interface — defines the contract that each platform
 * MQTT adapter must implement.
 *
 * Binding is link-time: exactly one platform .c file (e.g., mqtt_mosquitto.c
 * or mqtt_esp.c) is compiled into the final binary.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cynk_platform cynk_platform;

typedef void (*cynk_platform_msg_cb)(void *ctx, const char *topic,
                                     const void *payload, size_t len);

/* Create platform-specific context. Returns NULL on failure. */
cynk_platform *cynk_platform_create(void);

/* Connect to MQTT broker with device credentials and LWT. Blocking. */
int cynk_platform_connect(cynk_platform *p,
                          const char *device_id,
                          const char *password,
                          const char *broker_host,
                          int broker_port,
                          int use_tls,
                          const char *lwt_topic,
                          const void *lwt_payload,
                          size_t lwt_len);

/* MQTT publish — matches cynk_publish_fn signature. */
int cynk_platform_publish(void *ctx, const char *topic, const void *payload,
                          size_t len, int qos, int retain);

/* MQTT subscribe — matches cynk_subscribe_fn signature. */
int cynk_platform_subscribe(void *ctx, const char *topic, int qos);

/* Process network events. Dispatches received messages via the callback
   registered with cynk_platform_on_message. timeout_ms <= 0 means no wait. */
int cynk_platform_poll(cynk_platform *p, int timeout_ms);

/* Register callback invoked when an MQTT message arrives. */
void cynk_platform_on_message(cynk_platform *p, cynk_platform_msg_cb cb,
                              void *ctx);

/* Monotonic time in milliseconds. */
uint64_t cynk_platform_now_ms(void *ctx);

/* UTC ISO-8601 timestamp into buf. Returns 0 on success. */
int cynk_platform_now_iso8601(void *ctx, char *buf, size_t cap);

/* Disconnect from broker. */
void cynk_platform_disconnect(cynk_platform *p);

/* Free platform resources. */
void cynk_platform_destroy(cynk_platform *p);

#ifdef __cplusplus
}
#endif

#endif /* CYNK_PLATFORM_H */
