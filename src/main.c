#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sensors.h"
#include "wifi_mqtt.h"
#include "tasks.h"

#define TEMPO_DEEP_SLEEP 5000000 // 5 segundos (em microsegundos)
#define TAG "MONITORAMENTO"

SemaphoreHandle_t sensorMutex;
QueueHandle_t sensorQueue;

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

    // Configurar os canais ADC
    setup_adc_channels();

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
