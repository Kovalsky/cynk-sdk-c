#include "cynk.h"
#include "internal/cynk_protocol.h"
#include "internal/cynk_platform.h"

#include <stdlib.h>
#include <string.h>

#define CYNK_DEFAULT_BROKER "cynk.tech"
#define CYNK_DEFAULT_PORT 8883
#define CYNK_DEFAULT_TLS 1
#define CYNK_HANDSHAKE_POLL_MS 50
#define CYNK_LWT_PAYLOAD_MAX 160
#define CYNK_STATUS_ONLINE "online"
#define CYNK_STATUS_OFFLINE "offline"

struct cynk_device {
  cynk_proto *proto;
  cynk_platform *platform;
};

/* Callback bridging: platform delivers messages → protocol handles them. */
static void on_platform_message(void *ctx, const char *topic,
                                const void *payload, size_t len) {
  cynk_device *dev = (cynk_device *)ctx;
  cynk_proto_handle_message(dev->proto, topic, payload, len);
}

cynk_device *cynk_connect(const char *device_id, const char *password) {
  cynk_device *dev;
  cynk_proto_config cfg;
  cynk_transport tx;
  char lwt[CYNK_LWT_PAYLOAD_MAX];
  int rc;
  int connected = 0;

  if (!device_id || !password) {
    return NULL;
  }

  dev = (cynk_device *)calloc(1, sizeof(*dev));
  if (!dev) {
    return NULL;
  }

  dev->platform = cynk_platform_create();
  if (!dev->platform) {
    goto fail;
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.device_id = device_id;
  cfg.now_ms = cynk_platform_now_ms;
  cfg.now_iso8601 = cynk_platform_now_iso8601;
  cfg.time_ctx = dev->platform;

  memset(&tx, 0, sizeof(tx));
  tx.publish = cynk_platform_publish;
  tx.subscribe = cynk_platform_subscribe;
  tx.ctx = dev->platform;

  dev->proto = cynk_proto_create(&cfg, &tx);
  if (!dev->proto) {
    goto fail;
  }

  rc = cynk_proto_build_status_payload(dev->proto, CYNK_STATUS_OFFLINE,
                                       lwt, sizeof(lwt));
  if (rc != CYNK_OK) {
    goto fail;
  }

  rc = cynk_platform_connect(dev->platform, device_id, password,
                             CYNK_DEFAULT_BROKER, CYNK_DEFAULT_PORT,
                             CYNK_DEFAULT_TLS,
                             cynk_proto_status_topic(dev->proto),
                             lwt, strlen(lwt));
  if (rc != 0) {
    goto fail;
  }
  connected = 1;

  cynk_platform_on_message(dev->platform, on_platform_message, dev);

  rc = cynk_proto_on_connect(dev->proto);
  if (rc != CYNK_OK) {
    goto fail;
  }

  /* Block until handshake completes or times out. */
  while (!cynk_proto_handshake_ready(dev->proto)) {
    cynk_platform_poll(dev->platform, CYNK_HANDSHAKE_POLL_MS);
    rc = cynk_proto_poll(dev->proto);
    if (rc == CYNK_ERR_TIMEOUT) {
      goto fail;
    }
  }

  return dev;

fail:
  if (dev) {
    if (connected) {
      cynk_platform_disconnect(dev->platform);
    }
    if (dev->proto) {
      cynk_proto_destroy(dev->proto);
    }
    if (dev->platform) {
      cynk_platform_destroy(dev->platform);
    }
    free(dev);
  }
  return NULL;
}

int cynk_send(cynk_device *dev, const char *slug, double value) {
  cynk_widget_ref ref;
  cynk_value val;

  if (!dev || !slug) {
    return CYNK_ERR_INVALID_ARG;
  }

  memset(&ref, 0, sizeof(ref));
  ref.slug = slug;

  memset(&val, 0, sizeof(val));
  val.type = CYNK_VALUE_NUMBER;
  val.number = value;

  return cynk_proto_send_value(dev->proto, ref, val);
}

int cynk_send_bool(cynk_device *dev, const char *slug, int value) {
  cynk_widget_ref ref;
  cynk_value val;

  if (!dev || !slug) {
    return CYNK_ERR_INVALID_ARG;
  }

  memset(&ref, 0, sizeof(ref));
  ref.slug = slug;

  memset(&val, 0, sizeof(val));
  val.type = CYNK_VALUE_BOOL;
  val.boolean = value ? 1 : 0;

  return cynk_proto_send_value(dev->proto, ref, val);
}

int cynk_send_json(cynk_device *dev, const char *slug, const char *json) {
  cynk_widget_ref ref;
  cynk_value val;

  if (!dev || !slug || !json) {
    return CYNK_ERR_INVALID_ARG;
  }

  memset(&ref, 0, sizeof(ref));
  ref.slug = slug;

  memset(&val, 0, sizeof(val));
  val.type = CYNK_VALUE_JSON;
  val.json = json;

  return cynk_proto_send_value(dev->proto, ref, val);
}

void cynk_on_command(cynk_device *dev, cynk_command_cb cb, void *ctx) {
  if (!dev) {
    return;
  }
  cynk_proto_set_command_cb(dev->proto, cb, ctx);
}

int cynk_poll(cynk_device *dev, int timeout_ms) {
  int rc;
  if (!dev) {
    return CYNK_ERR_INVALID_ARG;
  }
  rc = cynk_platform_poll(dev->platform, timeout_ms);
  if (rc != 0) {
    return rc;
  }
  return cynk_proto_poll(dev->proto);
}

void cynk_disconnect(cynk_device *dev) {
  if (!dev) {
    return;
  }
  cynk_platform_disconnect(dev->platform);
  cynk_proto_destroy(dev->proto);
  cynk_platform_destroy(dev->platform);
  free(dev);
}
