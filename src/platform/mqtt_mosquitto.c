/*
 * Mosquitto MQTT platform adapter — desktop/Linux/Mac.
 *
 * Implements the cynk_platform interface (cynk_platform.h) using libmosquitto.
 * Linked into the cynk library at build time; resolved automatically by the
 * linker when the application links against -lcynk -lmosquitto.
 */

#define _POSIX_C_SOURCE 200112L

#include "internal/cynk_platform.h"

#include <mosquitto.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int mosq_lib_refs = 0;

struct cynk_platform {
  struct mosquitto *mosq;
  cynk_platform_msg_cb msg_cb;
  void *msg_ctx;
  int connected;
};

/* ---- Internal callbacks wired to libmosquitto ---- */

static void mosq_on_connect(struct mosquitto *mosq, void *userdata, int rc) {
  cynk_platform *p = (cynk_platform *)userdata;
  (void)mosq;
  if (rc == 0) {
    p->connected = 1;
  }
}

static void mosq_on_message(struct mosquitto *mosq, void *userdata,
                            const struct mosquitto_message *msg) {
  cynk_platform *p = (cynk_platform *)userdata;
  (void)mosq;
  if (p->msg_cb && msg->topic && msg->payload && msg->payloadlen > 0) {
    p->msg_cb(p->msg_ctx, msg->topic, msg->payload, (size_t)msg->payloadlen);
  }
}

/* Try to configure TLS certificate verification. Handles both mosquitto >= 2.0
   (OS cert store) and older versions (common CA bundle paths). */
static int setup_tls(struct mosquitto *mosq) {
  int rc;

#ifdef MOSQ_OPT_TLS_USE_OS_CERTS
  rc = mosquitto_int_option(mosq, MOSQ_OPT_TLS_USE_OS_CERTS, 1);
  if (rc == MOSQ_ERR_SUCCESS) {
    return 0;
  }
#endif

  /* Fallback: try common CA bundle paths. */
  static const char *ca_files[] = {
    "/etc/ssl/certs/ca-certificates.crt", /* Debian / Ubuntu */
    "/etc/pki/tls/certs/ca-bundle.crt",   /* RHEL / CentOS  */
    "/etc/ssl/cert.pem",                  /* macOS / Alpine  */
    NULL
  };

  for (int i = 0; ca_files[i]; i++) {
    rc = mosquitto_tls_set(mosq, ca_files[i], NULL, NULL, NULL, NULL);
    if (rc == MOSQ_ERR_SUCCESS) {
      return 0;
    }
  }

  /* Last resort: CA directory. */
  rc = mosquitto_tls_set(mosq, NULL, "/etc/ssl/certs", NULL, NULL, NULL);
  return rc == MOSQ_ERR_SUCCESS ? 0 : -1;
}

/* ---- Public platform interface (cynk_platform.h) ---- */

cynk_platform *cynk_platform_create(void) {
  cynk_platform *p = (cynk_platform *)calloc(1, sizeof(*p));
  if (!p) {
    return NULL;
  }
  if (mosq_lib_refs++ == 0) {
    mosquitto_lib_init();
  }
  return p;
}

int cynk_platform_connect(cynk_platform *p, const char *device_id,
                          const char *password, const char *broker_host,
                          int broker_port, int use_tls,
                          const char *lwt_topic, const void *lwt_payload,
                          size_t lwt_len) {
  int rc;
  int attempts;

  if (!p || !device_id || !password || !broker_host) {
    return -1;
  }

  /* Create mosquitto instance with device_id as MQTT client ID. */
  p->mosq = mosquitto_new(device_id, 1 /* clean session */, p);
  if (!p->mosq) {
    return -1;
  }

  mosquitto_connect_callback_set(p->mosq, mosq_on_connect);
  mosquitto_message_callback_set(p->mosq, mosq_on_message);

  rc = mosquitto_username_pw_set(p->mosq, device_id, password);
  if (rc != MOSQ_ERR_SUCCESS) {
    return -1;
  }

  if (use_tls) {
    if (setup_tls(p->mosq) != 0) {
      return -1;
    }
  }

  if (lwt_topic && lwt_payload && lwt_len > 0) {
    rc = mosquitto_will_set(p->mosq, lwt_topic, (int)lwt_len, lwt_payload,
                            1 /* qos */, 0 /* retain */);
    if (rc != MOSQ_ERR_SUCCESS) {
      return -1;
    }
  }

  rc = mosquitto_connect(p->mosq, broker_host, broker_port, 60 /* keepalive */);
  if (rc != MOSQ_ERR_SUCCESS) {
    return -1;
  }

  /* Wait for CONNACK (up to ~5 s). */
  p->connected = 0;
  for (attempts = 0; attempts < 50 && !p->connected; attempts++) {
    rc = mosquitto_loop(p->mosq, 100 /* ms */, 1);
    if (rc != MOSQ_ERR_SUCCESS) {
      return -1;
    }
  }

  return p->connected ? 0 : -1;
}

int cynk_platform_publish(void *ctx, const char *topic, const void *payload,
                          size_t len, int qos, int retain) {
  cynk_platform *p = (cynk_platform *)ctx;
  int rc = mosquitto_publish(p->mosq, NULL, topic, (int)len, payload, qos,
                             retain);
  return rc == MOSQ_ERR_SUCCESS ? 0 : -1;
}

int cynk_platform_subscribe(void *ctx, const char *topic, int qos) {
  cynk_platform *p = (cynk_platform *)ctx;
  int rc = mosquitto_subscribe(p->mosq, NULL, topic, qos);
  return rc == MOSQ_ERR_SUCCESS ? 0 : -1;
}

int cynk_platform_poll(cynk_platform *p, int timeout_ms) {
  int rc;
  if (!p || !p->mosq) {
    return -1;
  }
  rc = mosquitto_loop(p->mosq, timeout_ms > 0 ? timeout_ms : 0, 1);
  return rc == MOSQ_ERR_SUCCESS ? 0 : -1;
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
  struct timespec ts;
  (void)ctx;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
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
  if (p && p->mosq) {
    mosquitto_disconnect(p->mosq);
  }
}

void cynk_platform_destroy(cynk_platform *p) {
  if (!p) {
    return;
  }
  if (p->mosq) {
    mosquitto_destroy(p->mosq);
  }
  if (--mosq_lib_refs == 0) {
    mosquitto_lib_cleanup();
  }
  free(p);
}
