#include "tasks.h"
#include "sensors.h"
#include "wifi_mqtt.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_sleep.h"

extern const float tarifa_kwh;

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

void processar_tarefa(void *pvParameters) {
    sensor_t sensor;
    if (xQueueReceive(sensorQueue, &sensor, pdMS_TO_TICKS(1000))) {
        ESP_LOGI(TAG, "Processando dados: Aparelho = %s, Corrente = %f, Tensão = %f", 
                 sensor.device_id, sensor.corrente, sensor.tensao);

        // Enviar os dados processados para a fila para envio via MQTT
        if (xQueueSend(sensorQueue, &sensor, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "Dados enviados para a fila MQTT: Aparelho = %s, Corrente = %f, Tensão = %f", 
                     sensor.device_id, sensor.corrente, sensor.tensao);
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
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "device_id", sensor.device_id);
            cJSON_AddNumberToObject(root, "corrente", sensor.corrente);
            cJSON_AddNumberToObject(root, "tensao", sensor.tensao);

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

void gerenciar_tarefa(void *pvParameters) {
    ESP_LOGI(TAG, "Entrando em deep sleep por 5 segundos...");
    esp_sleep_enable_timer_wakeup(TEMPO_DEEP_SLEEP);
    esp_deep_sleep_start();
}
