/*
 * ESP32 Quick Start — Connect to cynk.tech, send telemetry.
 *
 * 1. Set WIFI_SSID, WIFI_PASS, DEVICE_ID, DEVICE_PASS below.
 * 2. Build and flash:
 *      cd examples/esp32
 *      idf.py build flash monitor
 *
 * Create a device at cynk.tech dashboard → Settings → Devices.
 */
#include "cynk.h"

#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

/* ---- Replace with your credentials ---- */
#define WIFI_SSID    "YOUR_SSID"
#define WIFI_PASS    "YOUR_PASSWORD"
#define DEVICE_ID    "YOUR_DEVICE_ID"
#define DEVICE_PASS  "YOUR_DEVICE_PASSWORD"

static const char *TAG = "cynk_example";
static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT BIT0

/* ---- Wi-Fi ---- */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data) {
  (void)arg;
  (void)data;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
  }
}

static void wifi_init(void) {
  s_wifi_events = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

  wifi_config_t wifi_cfg = {
    .sta = {
      .ssid = WIFI_SSID,
      .password = WIFI_PASS,
    },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
  ESP_ERROR_CHECK(esp_wifi_start());

  xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT,
                      pdFALSE, pdTRUE, portMAX_DELAY);
  ESP_LOGI(TAG, "Wi-Fi connected");
}

/* ---- SNTP time sync (required for timestamps) ---- */

static void sntp_sync(void) {
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  time_t now = 0;
  struct tm utc = {0};
  int retries = 0;
  while (utc.tm_year < (2020 - 1900) && retries < 30) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    time(&now);
    gmtime_r(&now, &utc);
    retries++;
  }
  ESP_LOGI(TAG, "Time synchronized");
}

/* ---- Cynk ---- */

static void on_command(void *ctx, const cynk_command *cmd) {
  (void)ctx;
  ESP_LOGI(TAG, "Command: %s (widget: %s)", cmd->command,
           cmd->widget.slug ? cmd->widget.slug : "?");
}

void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  wifi_init();
  sntp_sync();

  cynk_device *dev = cynk_connect(DEVICE_ID, DEVICE_PASS);
  if (!dev) {
    ESP_LOGE(TAG, "cynk_connect failed");
    return;
  }
  ESP_LOGI(TAG, "Connected to cynk.tech!");

  cynk_on_command(dev, on_command, NULL);

  /* Send telemetry every 5 seconds. */
  float temp = 22.0f;
  while (1) {
    cynk_send(dev, "temperature", (double)temp);
    ESP_LOGI(TAG, "Sent temperature=%.1f", temp);
    temp += 0.1f;

    cynk_poll(dev, 5000);
  }
}
