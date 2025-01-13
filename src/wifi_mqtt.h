#ifndef WIFI_MQTT_H
#define WIFI_MQTT_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"

extern bool mqtt_connected;
extern esp_mqtt_client_handle_t client;

void wifi_connection(void);
void mqtt_app_start(void);

#endif // WIFI_MQTT_H
