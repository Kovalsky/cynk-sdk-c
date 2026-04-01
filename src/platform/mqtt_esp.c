/*
 * ESP-IDF MQTT platform adapter — ESP32.
 *
 * Implements the cynk_platform interface (cynk_platform.h) using esp-mqtt.
 * Add the cynk-sdk-c directory as an ESP-IDF component; the build system
 * links this adapter automatically.
 */

#include "internal/cynk_platform.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"

static const char *TAG = "cynk";

#define CONNACK_BIT          BIT0
#define CONNFAIL_BIT         BIT1
#define CONNECT_TIMEOUT_MS   10000

struct cynk_platform {
  esp_mqtt_client_handle_t client;
  cynk_platform_msg_cb msg_cb;
  void *msg_ctx;
  EventGroupHandle_t events;
  int connected;
};

/* ---- Internal event handler wired to esp-mqtt ---- */

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  cynk_platform *p = (cynk_platform *)arg;
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  (void)base;

  switch (event_id) {
    case MQTT_EVENT_CONNECTED:
      p->connected = 1;
      xEventGroupSetBits(p->events, CONNACK_BIT);
      break;

    case MQTT_EVENT_DISCONNECTED:
      p->connected = 0;
      xEventGroupSetBits(p->events, CONNFAIL_BIT);
      break;

    case MQTT_EVENT_DATA:
      /* Only handle complete (non-fragmented) messages. esp-mqtt splits
         large payloads across multiple DATA events; Cynk protocol messages
         are small, so fragmented frames are ignored. */
      if (p->msg_cb && event->topic && event->data_len > 0 &&
          event->current_data_offset == 0 &&
          event->data_len == event->total_data_len) {
        /* esp-mqtt does not null-terminate the topic — copy it. */
        char topic_buf[256];
        int tlen = event->topic_len < (int)sizeof(topic_buf) - 1
                     ? event->topic_len
                     : (int)sizeof(topic_buf) - 1;
        memcpy(topic_buf, event->topic, (size_t)tlen);
        topic_buf[tlen] = '\0';

        p->msg_cb(p->msg_ctx, topic_buf, event->data,
                  (size_t)event->data_len);
      }
      break;

    case MQTT_EVENT_ERROR:
      ESP_LOGE(TAG, "MQTT error type=%d",
               event->error_handle->error_type);
      xEventGroupSetBits(p->events, CONNFAIL_BIT);
      break;

    default:
      break;
  }
}

/* ---- Public platform interface (cynk_platform.h) ---- */

cynk_platform *cynk_platform_create(void) {
  cynk_platform *p = (cynk_platform *)calloc(1, sizeof(*p));
  if (!p) {
    return NULL;
  }
  p->events = xEventGroupCreate();
  if (!p->events) {
    free(p);
    return NULL;
  }
  return p;
}

int cynk_platform_connect(cynk_platform *p, const char *device_id,
                          const char *password, const char *broker_host,
                          int broker_port, int use_tls,
                          const char *lwt_topic, const void *lwt_payload,
                          size_t lwt_len) {
  esp_mqtt_client_config_t mqtt_cfg;
  EventBits_t bits;

  if (!p || !device_id || !password || !broker_host) {
    return -1;
  }

  memset(&mqtt_cfg, 0, sizeof(mqtt_cfg));
  mqtt_cfg.broker.address.hostname = broker_host;
  mqtt_cfg.broker.address.port = (uint32_t)broker_port;
  mqtt_cfg.broker.address.transport =
      use_tls ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
  mqtt_cfg.credentials.username = device_id;
  mqtt_cfg.credentials.client_id = device_id;
  mqtt_cfg.credentials.authentication.password = password;
  mqtt_cfg.session.keepalive = 60;

  if (use_tls) {
    mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
  }

  if (lwt_topic && lwt_payload && lwt_len > 0) {
    mqtt_cfg.session.last_will.topic = lwt_topic;
    mqtt_cfg.session.last_will.msg = (const char *)lwt_payload;
    mqtt_cfg.session.last_will.msg_len = (int)lwt_len;
    mqtt_cfg.session.last_will.qos = 1;
    mqtt_cfg.session.last_will.retain = 0;
  }

  p->client = esp_mqtt_client_init(&mqtt_cfg);
  if (!p->client) {
    return -1;
  }

  esp_mqtt_client_register_event(p->client, ESP_EVENT_ANY_ID,
                                 mqtt_event_handler, p);

  if (esp_mqtt_client_start(p->client) != ESP_OK) {
    esp_mqtt_client_destroy(p->client);
    p->client = NULL;
    return -1;
  }

  /* Block until CONNACK or failure (10 s). */
  xEventGroupClearBits(p->events, CONNACK_BIT | CONNFAIL_BIT);
  bits = xEventGroupWaitBits(p->events, CONNACK_BIT | CONNFAIL_BIT,
                             pdTRUE, pdFALSE,
                             pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));

  if (!(bits & CONNACK_BIT)) {
    esp_mqtt_client_stop(p->client);
    esp_mqtt_client_destroy(p->client);
    p->client = NULL;
    return -1;
  }

  return 0;
}

int cynk_platform_publish(void *ctx, const char *topic, const void *payload,
                          size_t len, int qos, int retain) {
  cynk_platform *p = (cynk_platform *)ctx;
  int msg_id = esp_mqtt_client_publish(p->client, topic,
                                       (const char *)payload, (int)len,
                                       qos, retain);
  return msg_id >= 0 ? 0 : -1;
}

int cynk_platform_subscribe(void *ctx, const char *topic, int qos) {
  cynk_platform *p = (cynk_platform *)ctx;
  int msg_id = esp_mqtt_client_subscribe(p->client, topic, qos);
  return msg_id >= 0 ? 0 : -1;
}

int cynk_platform_poll(cynk_platform *p, int timeout_ms) {
  if (!p || !p->client) {
    return -1;
  }
  /* esp-mqtt runs its own FreeRTOS task; yield to let it process. */
  vTaskDelay(timeout_ms > 0 ? pdMS_TO_TICKS(timeout_ms) : 1);
  return p->connected ? 0 : -1;
}

void cynk_platform_on_message(cynk_platform *p, cynk_platform_msg_cb cb,
                              void *ctx) {
  if (!p) {
    return;
  }
  p->msg_cb = cb;
  p->msg_ctx = ctx;
}

uint64_t cynk_platform_now_ms(void *ctx) {
  (void)ctx;
  return (uint64_t)(esp_timer_get_time() / 1000);
}

int cynk_platform_now_iso8601(void *ctx, char *buf, size_t cap) {
  time_t now;
  struct tm utc;
  size_t n;
  (void)ctx;

  now = time(NULL);
  if (gmtime_r(&now, &utc) == NULL) {
    return -1;
  }
  n = strftime(buf, cap, "%Y-%m-%dT%H:%M:%SZ", &utc);
  return n > 0 ? 0 : -1;
}

void cynk_platform_disconnect(cynk_platform *p) {
  if (p && p->client) {
    esp_mqtt_client_disconnect(p->client);
    esp_mqtt_client_stop(p->client);
  }
}

void cynk_platform_destroy(cynk_platform *p) {
  if (!p) {
    return;
  }
  if (p->client) {
    esp_mqtt_client_destroy(p->client);
  }
  if (p->events) {
    vEventGroupDelete(p->events);
  }
  free(p);
}
