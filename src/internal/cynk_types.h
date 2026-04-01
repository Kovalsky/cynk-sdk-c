#ifndef CYNK_TYPES_H
#define CYNK_TYPES_H

/*
 * Shared type definitions used by both the public API (cynk.h)
 * and the internal protocol module (cynk_protocol.h).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
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
#define CYNK_ERR_CONNECT -10

/* Widget reference — at least one of id/slug must be set. */
typedef struct {
  const char *id;
  const char *slug;
} cynk_widget_ref;

/* Value types for telemetry payloads. */
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

/* Command received from dashboard. */
typedef struct {
  const char *command;
  const char *request_id;
  cynk_widget_ref widget;
  const char *params_json;
} cynk_command;

typedef void (*cynk_command_cb)(void *ctx, const cynk_command *cmd);

#ifdef __cplusplus
}
#endif

#endif /* CYNK_TYPES_H */
