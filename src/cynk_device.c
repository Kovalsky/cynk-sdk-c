#include "cynk_device.h"
#include "jsmn.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CYNK_JSON_TOKENS
#define CYNK_JSON_TOKENS 128
#endif

#define CYNK_STATUS_PAYLOAD_MAX 160

struct cynk_device {
  cynk_device_config cfg;
  cynk_transport tx;
  char *device_id;
  char *user_id;
  char status_topic[CYNK_TOPIC_MAX];
  char status_ack_topic[CYNK_TOPIC_MAX];
  char command_topic_wildcard[CYNK_TOPIC_MAX];
  char telemetry_topic[CYNK_TOPIC_MAX];
  int telemetry_topic_set;
  uint64_t handshake_started_ms;
  int handshake_in_progress;
  int handshake_ready;
  int handshake_timed_out;
  cynk_command_cb command_cb;
  void *command_ctx;
  cynk_handshake_cb handshake_cb;
  void *handshake_ctx;
};

static void *cynk_alloc(const cynk_device *dev, size_t size) {
  return dev->cfg.alloc ? dev->cfg.alloc(size) : malloc(size);
}

static void cynk_free(const cynk_device *dev, void *ptr) {
  if (!ptr) {
    return;
  }
  if (dev->cfg.free) {
    dev->cfg.free(ptr);
  } else {
    free(ptr);
  }
}

static char *cynk_strdup(const cynk_device *dev, const char *src) {
  size_t len = strlen(src);
  char *dest = (char *)cynk_alloc(dev, len + 1);
  if (!dest) {
    return NULL;
  }
  memcpy(dest, src, len);
  dest[len] = '\0';
  return dest;
}

static int cynk_topic_snprintf(char *buf, size_t cap, const char *fmt, const char *a) {
  int written = snprintf(buf, cap, fmt, a);
  if (written < 0 || (size_t)written >= cap) {
    return CYNK_ERR_BUFFER;
  }
  return CYNK_OK;
}

static int cynk_topic_snprintf2(char *buf, size_t cap, const char *fmt,
                                const char *a, const char *b) {
  int written = snprintf(buf, cap, fmt, a, b);
  if (written < 0 || (size_t)written >= cap) {
    return CYNK_ERR_BUFFER;
  }
  return CYNK_OK;
}

static int cynk_append(char *buf, size_t cap, size_t *pos, const char *fmt, ...) {
  va_list args;
  int written;

  if (*pos >= cap) {
    return CYNK_ERR_BUFFER;
  }

  va_start(args, fmt);
  written = vsnprintf(buf + *pos, cap - *pos, fmt, args);
  va_end(args);

  if (written < 0 || (size_t)written >= cap - *pos) {
    return CYNK_ERR_BUFFER;
  }

  *pos += (size_t)written;
  return CYNK_OK;
}

static int cynk_append_escaped(char *buf, size_t cap, size_t *pos, const char *value) {
  const unsigned char *p = (const unsigned char *)value;
  while (*p) {
    unsigned char c = *p++;
    switch (c) {
    case '\\':
    case '"':
      if (cynk_append(buf, cap, pos, "\\%c", c) != CYNK_OK) {
        return CYNK_ERR_BUFFER;
      }
      break;
    case '\b':
      if (cynk_append(buf, cap, pos, "\\b") != CYNK_OK) {
        return CYNK_ERR_BUFFER;
      }
      break;
    case '\f':
      if (cynk_append(buf, cap, pos, "\\f") != CYNK_OK) {
        return CYNK_ERR_BUFFER;
      }
      break;
    case '\n':
      if (cynk_append(buf, cap, pos, "\\n") != CYNK_OK) {
        return CYNK_ERR_BUFFER;
      }
      break;
    case '\r':
      if (cynk_append(buf, cap, pos, "\\r") != CYNK_OK) {
        return CYNK_ERR_BUFFER;
      }
      break;
    case '\t':
      if (cynk_append(buf, cap, pos, "\\t") != CYNK_OK) {
        return CYNK_ERR_BUFFER;
      }
      break;
    default:
      if (c < 0x20) {
        if (cynk_append(buf, cap, pos, "\\u%04x", c) != CYNK_OK) {
          return CYNK_ERR_BUFFER;
        }
      } else {
        if (cynk_append(buf, cap, pos, "%c", c) != CYNK_OK) {
          return CYNK_ERR_BUFFER;
        }
      }
      break;
    }
  }
  return CYNK_OK;
}

static int cynk_now_iso8601(const cynk_device *dev, char *buf, size_t cap) {
  if (!dev->cfg.now_iso8601) {
    return CYNK_ERR_TIME;
  }
  if (dev->cfg.now_iso8601(dev->cfg.time_ctx, buf, cap) != 0) {
    return CYNK_ERR_TIME;
  }
  return CYNK_OK;
}

static int cynk_topic_is_command(const cynk_device *dev, const char *topic) {
  const char *prefix = "cynk/v1/";
  size_t prefix_len = strlen(prefix);
  size_t topic_len = strlen(topic);
  size_t device_len = strlen(dev->device_id);
  size_t suffix_len = 1 + device_len + strlen("/command");

  if (topic_len <= prefix_len + suffix_len) {
    return 0;
  }
  if (strncmp(topic, prefix, prefix_len) != 0) {
    return 0;
  }

  if (topic_len < suffix_len) {
    return 0;
  }

  if (topic[topic_len - suffix_len] != '/') {
    return 0;
  }

  if (strncmp(topic + topic_len - suffix_len + 1, dev->device_id, device_len) != 0) {
    return 0;
  }

  if (strcmp(topic + topic_len - strlen("/command"), "/command") != 0) {
    return 0;
  }

  return 1;
}

static int cynk_json_eq(const char *json, const jsmntok_t *tok, const char *s) {
  size_t len = (size_t)(tok->end - tok->start);
  return tok->type == JSMN_STRING && strlen(s) == len &&
         strncmp(json + tok->start, s, len) == 0;
}

static int cynk_json_skip(const jsmntok_t *toks, int index) {
  int i = index;
  int count = 1;

  while (count > 0) {
    int children = 0;
    jsmntok_t tok = toks[i];

    if (tok.type == JSMN_OBJECT) {
      children = tok.size * 2;
    } else if (tok.type == JSMN_ARRAY) {
      children = tok.size;
    }

    count += children;
    count--;
    i++;
  }

  return i;
}

static int cynk_json_find_key(const char *json, const jsmntok_t *toks, int obj_index,
                              const char *key) {
  int pairs;
  int i;
  int p;

  if (toks[obj_index].type != JSMN_OBJECT) {
    return -1;
  }

  pairs = toks[obj_index].size;
  i = obj_index + 1;

  for (p = 0; p < pairs; p++) {
    int key_index = i;
    int val_index = i + 1;

    if (cynk_json_eq(json, &toks[key_index], key)) {
      return val_index;
    }

    i = cynk_json_skip(toks, val_index);
  }

  return -1;
}

static char *cynk_json_strdup(const cynk_device *dev, const char *json,
                              const jsmntok_t *tok) {
  size_t len = (size_t)(tok->end - tok->start);
  char *out = (char *)cynk_alloc(dev, len + 1);

  if (!out) {
    return NULL;
  }

  memcpy(out, json + tok->start, len);
  out[len] = '\0';
  return out;
}

static int cynk_parse_status_ack(cynk_device *dev, const char *json, size_t len) {
  jsmn_parser parser;
  jsmntok_t *tokens;
  int token_count;
  int user_index;
  int topics_index;
  int telemetry_index;
  char *user_id;

  tokens = (jsmntok_t *)cynk_alloc(dev, sizeof(jsmntok_t) * CYNK_JSON_TOKENS);
  if (!tokens) {
    return CYNK_ERR_NO_MEMORY;
  }

  jsmn_init(&parser);
  token_count = jsmn_parse(&parser, json, len, tokens, CYNK_JSON_TOKENS);
  if (token_count < 0) {
    cynk_free(dev, tokens);
    return CYNK_ERR_JSON;
  }

  user_index = cynk_json_find_key(json, tokens, 0, "user_id");
  if (user_index < 0 || tokens[user_index].type != JSMN_STRING) {
    cynk_free(dev, tokens);
    return CYNK_ERR_JSON;
  }

  user_id = cynk_json_strdup(dev, json, &tokens[user_index]);
  if (!user_id) {
    cynk_free(dev, tokens);
    return CYNK_ERR_NO_MEMORY;
  }

  if (dev->user_id) {
    cynk_free(dev, dev->user_id);
  }

  dev->user_id = user_id;
  dev->telemetry_topic_set = 0;

  topics_index = cynk_json_find_key(json, tokens, 0, "topics");
  if (topics_index >= 0 && tokens[topics_index].type == JSMN_OBJECT) {
    telemetry_index = cynk_json_find_key(json, tokens, topics_index, "telemetry");
    if (telemetry_index >= 0 && tokens[telemetry_index].type == JSMN_STRING) {
      size_t tlen = (size_t)(tokens[telemetry_index].end - tokens[telemetry_index].start);
      if (tlen < sizeof(dev->telemetry_topic)) {
        memcpy(dev->telemetry_topic, json + tokens[telemetry_index].start, tlen);
        dev->telemetry_topic[tlen] = '\0';
        dev->telemetry_topic_set = 1;
      }
    }
  }

  dev->handshake_ready = 1;
  dev->handshake_in_progress = 0;
  dev->handshake_timed_out = 0;

  if (dev->handshake_cb) {
    dev->handshake_cb(dev->handshake_ctx, dev->user_id);
  }

  cynk_free(dev, tokens);
  return CYNK_OK;
}

static int cynk_parse_command(cynk_device *dev, const char *json, size_t len) {
  jsmn_parser parser;
  jsmntok_t *tokens;
  int token_count;
  int command_index;
  int request_index;
  int widget_index;
  int params_index;
  int slug_index;
  int id_index;
  int rc = CYNK_OK;
  cynk_command cmd;

  memset(&cmd, 0, sizeof(cmd));

  tokens = (jsmntok_t *)cynk_alloc(dev, sizeof(jsmntok_t) * CYNK_JSON_TOKENS);
  if (!tokens) {
    return CYNK_ERR_NO_MEMORY;
  }

  jsmn_init(&parser);
  token_count = jsmn_parse(&parser, json, len, tokens, CYNK_JSON_TOKENS);
  if (token_count < 0) {
    rc = CYNK_ERR_JSON;
    goto cleanup;
  }

  command_index = cynk_json_find_key(json, tokens, 0, "command");
  if (command_index < 0 || tokens[command_index].type != JSMN_STRING) {
    rc = CYNK_ERR_JSON;
    goto cleanup;
  }

  cmd.command = cynk_json_strdup(dev, json, &tokens[command_index]);
  if (!cmd.command) {
    rc = CYNK_ERR_NO_MEMORY;
    goto cleanup;
  }

  request_index = cynk_json_find_key(json, tokens, 0, "request_id");
  if (request_index >= 0 && tokens[request_index].type == JSMN_STRING) {
    cmd.request_id = cynk_json_strdup(dev, json, &tokens[request_index]);
    if (!cmd.request_id) {
      rc = CYNK_ERR_NO_MEMORY;
      goto cleanup;
    }
  }

  widget_index = cynk_json_find_key(json, tokens, 0, "widget");
  if (widget_index >= 0 && tokens[widget_index].type == JSMN_OBJECT) {
    slug_index = cynk_json_find_key(json, tokens, widget_index, "slug");
    if (slug_index >= 0 && tokens[slug_index].type == JSMN_STRING) {
      cmd.widget.slug = cynk_json_strdup(dev, json, &tokens[slug_index]);
      if (!cmd.widget.slug) {
        rc = CYNK_ERR_NO_MEMORY;
        goto cleanup;
      }
    }

    id_index = cynk_json_find_key(json, tokens, widget_index, "id");
    if (id_index >= 0 && tokens[id_index].type == JSMN_STRING) {
      cmd.widget.id = cynk_json_strdup(dev, json, &tokens[id_index]);
      if (!cmd.widget.id) {
        rc = CYNK_ERR_NO_MEMORY;
        goto cleanup;
      }
    }
  }

  params_index = cynk_json_find_key(json, tokens, 0, "params");
  if (params_index >= 0) {
    cmd.params_json = cynk_json_strdup(dev, json, &tokens[params_index]);
    if (!cmd.params_json) {
      rc = CYNK_ERR_NO_MEMORY;
      goto cleanup;
    }
  }

  if (dev->command_cb) {
    dev->command_cb(dev->command_ctx, &cmd);
  }

cleanup:
  if (cmd.params_json) {
    cynk_free(dev, (char *)cmd.params_json);
  }
  if (cmd.widget.slug) {
    cynk_free(dev, (char *)cmd.widget.slug);
  }
  if (cmd.widget.id) {
    cynk_free(dev, (char *)cmd.widget.id);
  }
  if (cmd.request_id) {
    cynk_free(dev, (char *)cmd.request_id);
  }
  if (cmd.command) {
    cynk_free(dev, (char *)cmd.command);
  }
  cynk_free(dev, tokens);

  return rc;
}

static int cynk_telemetry_topic(const cynk_device *dev, char *buf, size_t cap) {
  if (dev->telemetry_topic_set) {
    size_t len = strlen(dev->telemetry_topic);
    if (len >= cap) {
      return CYNK_ERR_BUFFER;
    }
    memcpy(buf, dev->telemetry_topic, len + 1);
    return CYNK_OK;
  }

  if (!dev->user_id) {
    return CYNK_ERR_NO_HANDSHAKE;
  }

  return cynk_topic_snprintf2(buf, cap, "cynk/v1/%s/%s/telemetry", dev->user_id,
                              dev->device_id);
}

cynk_device *cynk_device_create(const cynk_device_config *cfg,
                                const cynk_transport *tx) {
  cynk_device *dev;

  if (!cfg || !tx || !tx->publish || !tx->subscribe || !cfg->device_id ||
      !cfg->now_ms || !cfg->now_iso8601) {
    return NULL;
  }
  if ((cfg->alloc && !cfg->free) || (!cfg->alloc && cfg->free)) {
    return NULL;
  }

  dev = (cynk_device *)(cfg->alloc ? cfg->alloc(sizeof(*dev)) : malloc(sizeof(*dev)));
  if (!dev) {
    return NULL;
  }
  memset(dev, 0, sizeof(*dev));

  dev->cfg = *cfg;
  dev->tx = *tx;
  dev->cfg.alloc = cfg->alloc ? cfg->alloc : NULL;
  dev->cfg.free = cfg->free ? cfg->free : NULL;
  dev->cfg.qos = cfg->qos > 0 ? cfg->qos : 1;
  dev->cfg.handshake_timeout_ms = cfg->handshake_timeout_ms > 0 ?
                                  cfg->handshake_timeout_ms : 5000;

  dev->device_id = cynk_strdup(dev, cfg->device_id);
  if (!dev->device_id) {
    cynk_device_destroy(dev);
    return NULL;
  }

  if (cynk_topic_snprintf(dev->status_topic, sizeof(dev->status_topic),
                          "cynk/v1/status/%s", dev->device_id) != CYNK_OK) {
    cynk_device_destroy(dev);
    return NULL;
  }

  if (cynk_topic_snprintf(dev->status_ack_topic, sizeof(dev->status_ack_topic),
                          "cynk/v1/status/%s/ack", dev->device_id) != CYNK_OK) {
    cynk_device_destroy(dev);
    return NULL;
  }

  if (cynk_topic_snprintf(dev->command_topic_wildcard,
                          sizeof(dev->command_topic_wildcard),
                          "cynk/v1/+/%s/command", dev->device_id) != CYNK_OK) {
    cynk_device_destroy(dev);
    return NULL;
  }

  return dev;
}

void cynk_device_destroy(cynk_device *dev) {
  if (!dev) {
    return;
  }
  if (dev->device_id) {
    cynk_free(dev, dev->device_id);
  }
  if (dev->user_id) {
    cynk_free(dev, dev->user_id);
  }
  cynk_free(dev, dev);
}

int cynk_device_on_connect(cynk_device *dev) {
  char payload[CYNK_STATUS_PAYLOAD_MAX];
  int rc;

  if (!dev) {
    return CYNK_ERR_INVALID_ARG;
  }

  rc = dev->tx.subscribe(dev->tx.ctx, dev->status_ack_topic, dev->cfg.qos);
  if (rc != 0) {
    return CYNK_ERR_SUBSCRIBE;
  }

  rc = dev->tx.subscribe(dev->tx.ctx, dev->command_topic_wildcard, dev->cfg.qos);
  if (rc != 0) {
    return CYNK_ERR_SUBSCRIBE;
  }

  rc = cynk_build_status_payload(dev, "online", payload, sizeof(payload));
  if (rc != CYNK_OK) {
    return rc;
  }

  rc = dev->tx.publish(dev->tx.ctx, dev->status_topic, payload, strlen(payload),
                       dev->cfg.qos, 0);
  if (rc != 0) {
    return CYNK_ERR_PUBLISH;
  }

  dev->handshake_started_ms = dev->cfg.now_ms(dev->cfg.time_ctx);
  dev->handshake_in_progress = 1;
  dev->handshake_ready = 0;
  dev->handshake_timed_out = 0;

  return CYNK_OK;
}

int cynk_device_handle_message(cynk_device *dev, const char *topic,
                               const void *payload, size_t len) {
  const char *json = (const char *)payload;

  if (!dev || !topic || !payload) {
    return CYNK_ERR_INVALID_ARG;
  }

  if (strcmp(topic, dev->status_ack_topic) == 0) {
    return cynk_parse_status_ack(dev, json, len);
  }

  if (cynk_topic_is_command(dev, topic)) {
    if (dev->command_cb) {
      return cynk_parse_command(dev, json, len);
    }
    return CYNK_OK;
  }

  return CYNK_OK;
}

int cynk_device_poll(cynk_device *dev) {
  uint64_t now;

  if (!dev) {
    return CYNK_ERR_INVALID_ARG;
  }

  if (!dev->handshake_in_progress || dev->handshake_ready || dev->handshake_timed_out) {
    return CYNK_OK;
  }

  now = dev->cfg.now_ms(dev->cfg.time_ctx);
  if (now - dev->handshake_started_ms >= dev->cfg.handshake_timeout_ms) {
    dev->handshake_timed_out = 1;
    dev->handshake_in_progress = 0;
    return CYNK_ERR_TIMEOUT;
  }

  return CYNK_OK;
}

int cynk_device_handshake_ready(const cynk_device *dev) {
  return dev && dev->handshake_ready;
}

const char *cynk_device_user_id(const cynk_device *dev) {
  return dev ? dev->user_id : NULL;
}

void cynk_device_set_command_cb(cynk_device *dev, cynk_command_cb cb, void *ctx) {
  if (!dev) {
    return;
  }
  dev->command_cb = cb;
  dev->command_ctx = ctx;
}

void cynk_device_set_handshake_cb(cynk_device *dev, cynk_handshake_cb cb, void *ctx) {
  if (!dev) {
    return;
  }
  dev->handshake_cb = cb;
  dev->handshake_ctx = ctx;
}

int cynk_device_send_value(cynk_device *dev, cynk_widget_ref ref, cynk_value value) {
  char ts[CYNK_TS_MAX];
  char topic[CYNK_TOPIC_MAX];
  char *payload;
  size_t payload_cap;
  size_t pos = 0;
  int rc;
  int first_field = 1;

  if (!dev) {
    return CYNK_ERR_INVALID_ARG;
  }
  if (!ref.id && !ref.slug) {
    return CYNK_ERR_INVALID_ARG;
  }

  rc = cynk_now_iso8601(dev, ts, sizeof(ts));
  if (rc != CYNK_OK) {
    return rc;
  }

  rc = cynk_telemetry_topic(dev, topic, sizeof(topic));
  if (rc != CYNK_OK) {
    return rc;
  }

  payload_cap = 256;
  if (ref.id) {
    payload_cap += strlen(ref.id) * 6;
  }
  if (ref.slug) {
    payload_cap += strlen(ref.slug) * 6;
  }
  if (value.type == CYNK_VALUE_STRING && value.string) {
    payload_cap += strlen(value.string) * 6;
  } else if (value.type == CYNK_VALUE_JSON && value.json) {
    payload_cap += strlen(value.json);
  }

  payload = (char *)cynk_alloc(dev, payload_cap);
  if (!payload) {
    return CYNK_ERR_NO_MEMORY;
  }

  rc = cynk_append(payload, payload_cap, &pos, "{\"ts\":\"");
  if (rc == CYNK_OK) {
    rc = cynk_append_escaped(payload, payload_cap, &pos, ts);
  }
  if (rc == CYNK_OK) {
    rc = cynk_append(payload, payload_cap, &pos, "\",\"widgets\":[{");
  }

  if (rc == CYNK_OK && ref.id) {
    rc = cynk_append(payload, payload_cap, &pos, "\"id\":\"");
    if (rc == CYNK_OK) {
      rc = cynk_append_escaped(payload, payload_cap, &pos, ref.id);
    }
    if (rc == CYNK_OK) {
      rc = cynk_append(payload, payload_cap, &pos, "\"");
    }
    first_field = 0;
  }

  if (rc == CYNK_OK && ref.slug) {
    if (!first_field) {
      rc = cynk_append(payload, payload_cap, &pos, ",");
    }
    if (rc == CYNK_OK) {
      rc = cynk_append(payload, payload_cap, &pos, "\"slug\":\"");
    }
    if (rc == CYNK_OK) {
      rc = cynk_append_escaped(payload, payload_cap, &pos, ref.slug);
    }
    if (rc == CYNK_OK) {
      rc = cynk_append(payload, payload_cap, &pos, "\"");
    }
    first_field = 0;
  }

  if (rc == CYNK_OK) {
    if (!first_field) {
      rc = cynk_append(payload, payload_cap, &pos, ",");
    }
  }
  if (rc == CYNK_OK) {
    rc = cynk_append(payload, payload_cap, &pos, "\"payload\":{\"value\":");
  }

  if (rc == CYNK_OK) {
    switch (value.type) {
    case CYNK_VALUE_NUMBER:
      rc = cynk_append(payload, payload_cap, &pos, "%.17g", value.number);
      break;
    case CYNK_VALUE_BOOL:
      rc = cynk_append(payload, payload_cap, &pos, value.boolean ? "true" : "false");
      break;
    case CYNK_VALUE_STRING:
      if (!value.string) {
        rc = CYNK_ERR_INVALID_ARG;
        break;
      }
      rc = cynk_append(payload, payload_cap, &pos, "\"");
      if (rc == CYNK_OK) {
        rc = cynk_append_escaped(payload, payload_cap, &pos, value.string);
      }
      if (rc == CYNK_OK) {
        rc = cynk_append(payload, payload_cap, &pos, "\"");
      }
      break;
    case CYNK_VALUE_JSON:
      if (!value.json) {
        rc = CYNK_ERR_INVALID_ARG;
        break;
      }
      rc = cynk_append(payload, payload_cap, &pos, "%s", value.json);
      break;
    default:
      rc = CYNK_ERR_INVALID_ARG;
      break;
    }
  }

  if (rc == CYNK_OK) {
    rc = cynk_append(payload, payload_cap, &pos, "}}]}");
  }

  if (rc == CYNK_OK) {
    rc = dev->tx.publish(dev->tx.ctx, topic, payload, pos, dev->cfg.qos, 0);
    if (rc != 0) {
      rc = CYNK_ERR_PUBLISH;
    } else {
      rc = CYNK_OK;
    }
  }

  cynk_free(dev, payload);
  return rc;
}

int cynk_device_send_raw(cynk_device *dev, const char *telemetry_json, size_t len) {
  char topic[CYNK_TOPIC_MAX];
  int rc;

  if (!dev || !telemetry_json || len == 0) {
    return CYNK_ERR_INVALID_ARG;
  }

  rc = cynk_telemetry_topic(dev, topic, sizeof(topic));
  if (rc != CYNK_OK) {
    return rc;
  }

  rc = dev->tx.publish(dev->tx.ctx, topic, telemetry_json, len, dev->cfg.qos, 0);
  if (rc != 0) {
    return CYNK_ERR_PUBLISH;
  }

  return CYNK_OK;
}

const char *cynk_device_status_topic(const cynk_device *dev) {
  return dev ? dev->status_topic : NULL;
}

const char *cynk_device_status_ack_topic(const cynk_device *dev) {
  return dev ? dev->status_ack_topic : NULL;
}

const char *cynk_device_command_topic_wildcard(const cynk_device *dev) {
  return dev ? dev->command_topic_wildcard : NULL;
}

int cynk_build_status_payload(const cynk_device *dev, const char *status,
                              char *buf, size_t cap) {
  char ts[CYNK_TS_MAX];
  size_t pos = 0;
  int rc;

  if (!dev || !status || !buf || cap == 0) {
    return CYNK_ERR_INVALID_ARG;
  }

  rc = cynk_now_iso8601(dev, ts, sizeof(ts));
  if (rc != CYNK_OK) {
    return rc;
  }

  rc = cynk_append(buf, cap, &pos, "{\"status\":\"");
  if (rc == CYNK_OK) {
    rc = cynk_append_escaped(buf, cap, &pos, status);
  }
  if (rc == CYNK_OK) {
    rc = cynk_append(buf, cap, &pos, "\",\"device_id\":\"");
  }
  if (rc == CYNK_OK) {
    rc = cynk_append_escaped(buf, cap, &pos, dev->device_id);
  }
  if (rc == CYNK_OK) {
    rc = cynk_append(buf, cap, &pos, "\",\"ts\":\"");
  }
  if (rc == CYNK_OK) {
    rc = cynk_append_escaped(buf, cap, &pos, ts);
  }
  if (rc == CYNK_OK) {
    rc = cynk_append(buf, cap, &pos, "\"}");
  }

  return rc;
}
