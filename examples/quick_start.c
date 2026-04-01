/*
 * Quick Start — Connect to cynk.tech, send a value, disconnect.
 *
 * Requires: libmosquitto-dev installed, a cynk.tech device created via the
 *           dashboard (Settings → Devices → copy device_id and password).
 *
 * Build:  cmake -B build && cmake --build build
 * Usage:  ./build/cynk_quick_start <device_id> <password>
 */
#include "cynk.h"

#include <stdio.h>

int main(int argc, char **argv) {
  cynk_device *dev;
  int rc;

  if (argc != 3) {
    fprintf(stderr, "Usage: %s <device_id> <password>\n", argv[0]);
    return 1;
  }

  dev = cynk_connect(argv[1], argv[2]);
  if (!dev) {
    fprintf(stderr, "Connection failed\n");
    return 1;
  }
  printf("Connected!\n");

  rc = cynk_send(dev, "temperature", 23.5);
  if (rc != 0) {
    fprintf(stderr, "Send failed: %d\n", rc);
  } else {
    printf("Sent temperature=23.5\n");
  }

  rc = cynk_send_bool(dev, "heater", 1);
  if (rc != 0) {
    fprintf(stderr, "Send bool failed: %d\n", rc);
  } else {
    printf("Sent heater=true\n");
  }

  cynk_disconnect(dev);
  printf("Disconnected.\n");
  return 0;
}
