#include "wifi_mqtt.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include <secrets.h>

#define TAG "WIFI_MQTT"

bool mqtt_connected = false; // Flag para verificar conexão MQTT
esp_mqtt_client_handle_t client; // Definição global

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Connected!");
        esp_mqtt_client_subscribe(client, "energy_monitoring_rafa", 2);
        mqtt_connected = true; // Define a flag de conexão como verdadeira
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT Disconnected!");
        mqtt_connected = false; // Define a flag de conexão como falsa
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "Mensagem publicada com sucesso!");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT Subscribed!");
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT Unsubscribed!");
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT Data Received: %.*s", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT Error!");
        break;
    default:
        ESP_LOGI(TAG, "MQTT Event ID: %d", event->event_id);
        break;
    }
}

void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://test.mosquitto.org",
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    ESP_LOGI(TAG, "Starting MQTT... ");
    esp_mqtt_client_start(client);
}

static void wifi_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "Wifi connecting...");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "Wifi connected!");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "Wifi disconnected! Tentando reconectar...");
        esp_wifi_connect();
        break;
    case IP_EVENT_STA_GOT_IP:
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char ip[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip, sizeof(ip));
        ESP_LOGI(TAG, "Wifi got IP: %s", ip);

        // Conectar ao MQTT após obter IP
        mqtt_app_start();
        break;
    }
    default:
        ESP_LOGI(TAG, "EVENT ID: %ld", event_id);
        break;
    }
}

void wifi_connection(void)
{
    // 1 - WiFi LwIP Init Phase
    esp_netif_init();                    // TCP/IP initiation
    esp_event_loop_create_default();     // Event Loop
    esp_netif_create_default_wifi_sta(); // WiFi Station
    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_initiation);
    // 2 - WiFi Configuration Phase
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    wifi_config_t wifi_configuration = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_configuration);
    // 3 - WiFi Start Phase
    esp_wifi_start();
    // 4 - Connect Phase
    esp_wifi_connect();
}
