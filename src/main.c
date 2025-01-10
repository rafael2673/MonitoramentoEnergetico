#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_sleep.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_check.h"
#include "esp_adc/adc_oneshot.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "stdlib.h"
#include "stdio.h"
#include "sys/time.h"

#include "mqtt_client.h"
#include "cJSON.h"
#include "driver/dac_cosine.h"
#include <secrets.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#define TEMPO_DEEP_SLEEP 5000000 // 5 segundos (em microsegundos)
#define TAG "MONITORAMENTO"
#define DAC_CHANNEL_VOLTAGE DAC_CHAN_0    // GPIO 25 para Tensão
#define DAC_CHANNEL_CURRENT DAC_CHAN_1    // GPIO 26 para Corrente
#define ADC_CHANNEL_VOLTAGE ADC_CHANNEL_0 // GPIO 36
#define ADC_CHANNEL_CURRENT ADC_CHANNEL_3 // GPIO 39
#define ADC_WIDTH ADC_BITWIDTH_12
#define ADC_ATTEN ADC_ATTEN_DB_12

SemaphoreHandle_t sensorMutex;
QueueHandle_t sensorQueue;
adc_oneshot_unit_handle_t adc_handle;
dac_cosine_handle_t dac_handle_voltage;
dac_cosine_handle_t dac_handle_current;
esp_mqtt_client_handle_t client; // Definição global

typedef struct {
    const char* device_id;
    int adc_channel_voltage;
    int adc_channel_current;
    float corrente;
    float tensao;
    float energia_total;
    float custo;
} sensor_t;

#define MAX_SENSORS 8

sensor_t sensores[MAX_SENSORS];
int num_sensores = 0;
const float tarifa_kwh = 0.095;
bool mqtt_connected = false; // Flag para verificar conexão MQTT

void wifi_connection(void);
static void wifi_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void mqtt_app_start(void);

void adicionar_sensor(const char* device_id, int adc_channel_voltage, int adc_channel_current) {
    if (num_sensores < MAX_SENSORS) {
        sensores[num_sensores].device_id = device_id;
        sensores[num_sensores].adc_channel_voltage = adc_channel_voltage;
        sensores[num_sensores].adc_channel_current = adc_channel_current;
        sensores[num_sensores].corrente = 0.0;
        sensores[num_sensores].tensao = 0.0;
        sensores[num_sensores].energia_total = 0.0;
        num_sensores++;
    } else {
        ESP_LOGE(TAG, "Número máximo de sensores atingido");
    }
}
void ler_valores_sensores() {
    for (int i = 0; i < num_sensores; i++) {
        int adc_val_voltage = 0;
        int adc_val_current = 0;
        adc_oneshot_read(adc_handle, sensores[i].adc_channel_voltage, &adc_val_voltage);
        adc_oneshot_read(adc_handle, sensores[i].adc_channel_current, &adc_val_current);
        sensores[i].tensao = (float)adc_val_voltage / 4095.0 * 220.0;
        sensores[i].corrente = (float)adc_val_current / 4095.0 * 15.0;
        ESP_LOGI(TAG, "Leitura Sensor: %s | Corrente: %.2f A | Tensão: %.2f V",
                 sensores[i].device_id, sensores[i].corrente, sensores[i].tensao);
    }
}

void calcular_energia_sensores() {
    for (int i = 0; i < num_sensores; i++) {
        float potencia = sensores[i].corrente * sensores[i].tensao;
        float energia = potencia * (5.0 / 3600.0);
        sensores[i].energia_total += energia;
        float custo = sensores[i].energia_total * tarifa_kwh;
        ESP_LOGI(TAG, "Sensor: %s | Potência: %.2f W | Energia total: %.4f kWh | Custo: R$ %.2f",
                 sensores[i].device_id, potencia, sensores[i].energia_total, custo);
    }
}


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

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://test.mosquitto.org",
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    printf("Starting MQTT... \n");
    esp_mqtt_client_start(client);
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

static void wifi_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        printf("Wifi connecting...\n");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        printf("Wifi connected!\n");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        printf("Wifi disconnected! Tentando reconectar...\n");
        esp_wifi_connect();
        break;
    case IP_EVENT_STA_GOT_IP:
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char ip[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip, sizeof(ip));
        printf("Wifi got IP: %s\n", ip);

        // Conectar ao MQTT após obter IP
        mqtt_app_start();
        break;
    }
    default:
        printf("EVENT ID: %ld\n", event_id);
        break;
    }
}

void generate_voltage_wave(dac_channel_t channel) {
    dac_cosine_config_t dac_cfg_voltage = {
        .chan_id = channel,
        .freq_hz = 1000,
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
        .offset = 0,
        .phase = DAC_COSINE_PHASE_0,
        .atten = DAC_COSINE_ATTEN_DEFAULT,
        .flags.force_set_freq = true,
    };

    dac_cosine_handle_t dac_handle;
    ESP_ERROR_CHECK(dac_cosine_new_channel(&dac_cfg_voltage, &dac_handle));
    ESP_ERROR_CHECK(dac_cosine_start(dac_handle));
}


void generate_current_wave(dac_channel_t channel) {
    dac_cosine_config_t dac_cfg_current = {
        .chan_id = channel,
        .freq_hz = 1000,
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
        .offset = 0,
        .phase = DAC_COSINE_PHASE_0,
        .atten = DAC_COSINE_ATTEN_DEFAULT,
        .flags.force_set_freq = true,
    };

    dac_cosine_handle_t dac_handle;
    ESP_ERROR_CHECK(dac_cosine_new_channel(&dac_cfg_current, &dac_handle));
    ESP_ERROR_CHECK(dac_cosine_start(dac_handle));
}


void read_voltage_value(int adc_channel, float *voltage) {
    int adc_val_voltage = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, adc_channel, &adc_val_voltage));
    *voltage = (float)adc_val_voltage / 4095.0 * 220.0; // Assume 12-bit ADC and 0-220V range
}


void read_current_value(int adc_channel, float *current) {
    int adc_val_current = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, adc_channel, &adc_val_current));
    *current = (float)adc_val_current / 4095.0 * 15.0; // Assume 12-bit ADC and 0-15A range
}


void calcular_energia(sensor_t* sensor)
{
    float potencia = sensor->corrente * sensor->tensao;

    // Calcula energia consumida em 5 segundos (5/3600 horas)
    float energia = potencia * (5.0 / 3600.0);
    sensor->energia_total += energia;

    sensor->custo = sensor->energia_total * tarifa_kwh;

    ESP_LOGI(TAG, "Potência: %.2f W | Energia total: %.4f kWh | Custo: R$ %.2f",
             potencia, sensor->energia_total, sensor->custo);
}

// Função para salvar energia total no NVS
void salvar_energia_total(sensor_t* sensor) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        char key[16];
        snprintf(key, sizeof(key), "energia_%s", sensor->device_id);
        nvs_set_blob(handle, key, &sensor->energia_total, sizeof(sensor->energia_total));
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Energia total do %s salva: %.4f kWh", sensor->device_id, sensor->energia_total);
    } else {
        ESP_LOGE(TAG, "Erro ao abrir NVS para salvar energia total do %s", sensor->device_id);
    }
}


// Função para carregar energia total do NVS
void carregar_energia_total(sensor_t* sensor) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        char key[16];
        snprintf(key, sizeof(key), "energia_%s", sensor->device_id);
        size_t required_size = sizeof(sensor->energia_total);
        err = nvs_get_blob(handle, key, &sensor->energia_total, &required_size);
        nvs_close(handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Energia total do %s carregada: %.4f kWh", sensor->device_id, sensor->energia_total);
        } else {
            ESP_LOGE(TAG, "Falha ao carregar energia total do %s do NVS: %s", sensor->device_id, esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "Erro ao abrir NVS para carregar energia total do %s: %s", sensor->device_id, esp_err_to_name(err));
    }
}


// Tarefa 1: Gerar e enviar dados de sensores para a fila
void simular_tarefa(void *pvParameters) {
    for (int i = 0; i < num_sensores; i++) {
        // Protege leituras com semaforo
        xSemaphoreTake(sensorMutex, portMAX_DELAY);
        read_current_value(sensores[i].adc_channel_current, &sensores[i].corrente);
        read_voltage_value(sensores[i].adc_channel_voltage, &sensores[i].tensao);
        xSemaphoreGive(sensorMutex);

        // Envia os dados para a fila
        if (xQueueSend(sensorQueue, &sensores[i], portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "Dados enviados para a fila: Aparelho = %s, Corrente = %.2f A, Tensão = %.2f V",
                     sensores[i].device_id, sensores[i].corrente, sensores[i].tensao);
        }
    }

    vTaskDelete(NULL); // Tarefa executada uma vez
}



void enviar_dados_mqtt_tarefa(void *pvParameters) {
    sensor_t sensor;
    if (xQueueReceive(sensorQueue, &sensor, pdMS_TO_TICKS(1000))) {
        if (!mqtt_connected) {
            ESP_LOGE(TAG, "MQTT client is not connected!");
            vTaskDelete(NULL);
            return;
        }

        if (sensor.corrente != 0.0 && sensor.tensao != 0.0) {
            ESP_LOGI("ENVIO DE MENSAGEM", "montando a mensagem");
            float custo = sensor.energia_total * tarifa_kwh;
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "device_id", sensor.device_id);
            cJSON_AddNumberToObject(root, "corrente", sensor.corrente);
            cJSON_AddNumberToObject(root, "tensao", sensor.tensao);
            cJSON_AddNumberToObject(root, "energia_total", sensor.energia_total);
            cJSON_AddNumberToObject(root, "custo", custo);

            char *buffer = cJSON_Print(root);

            int msg_id = esp_mqtt_client_publish(client, "energy_monitoring_rafa", buffer, 0, 0, 0);
            ESP_LOGI("ENVIO DE MENSAGEM", "Mensagem enviada, msg_id=%d", msg_id);

            cJSON_Delete(root); // Isso já libera a memória alocada por cJSON_Print
        } else {
            ESP_LOGI("ENVIO DE MENSAGEM", "corrente ou tensão igual a zero");
        }
    }

    vTaskDelete(NULL); // Tarefa executada uma vez
}


// Tarefa 2: Processar dados da fila
void processar_tarefa(void *pvParameters) {
    sensor_t sensor;
    if (xQueueReceive(sensorQueue, &sensor, pdMS_TO_TICKS(1000))) {
        ESP_LOGI(TAG, "Processando dados: Aparelho = %s, Corrente = %f, Tensão = %f", 
                 sensor.device_id, sensor.corrente, sensor.tensao);

        calcular_energia(&sensor);
        salvar_energia_total(&sensor);

        // Enviar os dados processados para a fila para envio via MQTT
        if (xQueueSend(sensorQueue, &sensor, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "Dados enviados para a fila MQTT: Aparelho = %s, Corrente = %f, Tensão = %f", 
                     sensor.device_id, sensor.corrente, sensor.tensao);
        }
    }

    vTaskDelete(NULL); // Tarefa executada uma vez
}


// Tarefa 3: Gerenciar exibição e modo de economia de energia
void gerenciar_tarefa(void *pvParameters)
{
    // Carrega a energia total de todos os sensores do NVS para manter a persistência após o deep sleep
    for (int i = 0; i < num_sensores; i++) {
        carregar_energia_total(&sensores[i]);
    }

    ESP_LOGI(TAG, "Entrando em deep sleep por 5 segundos...");
    esp_sleep_enable_timer_wakeup(TEMPO_DEEP_SLEEP);
    esp_deep_sleep_start();
}


void setup_adc_channels() {
    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = false,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_cfg, &adc_handle));
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_WIDTH,
    };

    // Configurar todos os canais ADC utilizados
    for (int i = 0; i < num_sensores; i++) {
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, sensores[i].adc_channel_voltage, &chan_cfg));
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, sensores[i].adc_channel_current, &chan_cfg));
    }
}


void app_main() {
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_LOGE(TAG, "Erro ao inicializar NVS");
        return;
    }

    // Conectar ao WiFi 
    wifi_connection(); 
    // Aguarda tempo para conexão WiFi 
    vTaskDelay(pdMS_TO_TICKS(5000));

    sensorMutex = xSemaphoreCreateMutex();
    if (sensorMutex == NULL) {
        ESP_LOGE(TAG, "Falha ao criar mutex");
        return;
    }

    sensorQueue = xQueueCreate(10, sizeof(sensor_t));
    if (sensorQueue == NULL) {
        ESP_LOGE(TAG, "Falha ao criar fila");
        return;
    }

    // Adicionar sensores dinamicamente
    adicionar_sensor("device_1", ADC_CHANNEL_0, ADC_CHANNEL_3);
    //adicionar_sensor("device_2", ADC_CHANNEL_6, ADC_CHANNEL_7);
    // Continue adicionando sensores conforme necessário

    // Inicializar DACs para múltiplos sensores
    generate_voltage_wave(DAC_CHAN_0); // Exemplo, altere conforme necessário
    generate_current_wave(DAC_CHAN_1); // Exemplo, altere conforme necessário
    //generate_voltage_wave(DAC_CHANNEL_VOLTAGE_2); // Exemplo, altere conforme necessário
    //generate_current_wave(DAC_CHANNEL_CURRENT_2); // Exemplo, altere conforme necessário

    // Configurar os canais ADC
    setup_adc_channels();

    // Carregar energia total de todos os sensores
    for (int i = 0; i < num_sensores; i++) {
        carregar_energia_total(&sensores[i]);
    }

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG, "ESP acordou do deep sleep");
    } else {
        ESP_LOGI(TAG, "Inicializando o sistema pela primeira vez");
    }

    // Cria tarefas
    xTaskCreate(simular_tarefa, "SimularTarefa", 2048, NULL, 5, NULL);
    xTaskCreate(processar_tarefa, "ProcessarTarefa", 4096, NULL, 5, NULL);
    vTaskDelay(pdMS_TO_TICKS(500));
    xTaskCreate(enviar_dados_mqtt_tarefa, "EnviarDadosMQTTTarefa", 4096, NULL, 5, NULL);
    xTaskCreate(gerenciar_tarefa, "GerenciarTarefa", 2048, NULL, 5, NULL);
}
