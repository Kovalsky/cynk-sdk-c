#ifndef CYNK_H
#define CYNK_H

/*
 * Cynk Device SDK — public API
 *
 * Connect to cynk.tech and send telemetry in three lines:
 *
 *   cynk_device *dev = cynk_connect("my_device", "my_password");
 *   cynk_send(dev, "temperature", 25.5);
 *   cynk_disconnect(dev);
 */

#include "internal/cynk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cynk_device cynk_device;

/*
 * Connect to cynk.tech with device credentials.
 * Blocks until handshake completes or times out (default 5 s).
 * Returns NULL on failure.
 */
cynk_device *cynk_connect(const char *device_id, const char *password);

/* Send a numeric value to a widget by slug. */
int cynk_send(cynk_device *dev, const char *slug, double value);

/* Send a boolean value to a widget by slug. */
int cynk_send_bool(cynk_device *dev, const char *slug, int value);

/* Send a raw JSON value to a widget by slug. */
int cynk_send_json(cynk_device *dev, const char *slug, const char *json);

/* Register a callback for incoming commands from the dashboard. */
void cynk_on_command(cynk_device *dev, cynk_command_cb cb, void *ctx);

/*
 * Process network events and dispatch incoming messages.
 * Call periodically to receive commands. timeout_ms <= 0 means no wait.
 */
int cynk_poll(cynk_device *dev, int timeout_ms);

/* Disconnect from cynk.tech and free all resources. */
void cynk_disconnect(cynk_device *dev);

#ifdef __cplusplus
}
#endif

#endif /* CYNK_H */
