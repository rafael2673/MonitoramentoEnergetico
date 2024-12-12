#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include "driver/dac_cosine.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"

// Definições
#define TEMPO_DEEP_SLEEP 5000000 // 5 segundos (em microsegundos)
#define TAG "MONITORAMENTO"
#define DAC_CHANNEL_VOLTAGE DAC_CHAN_0 // GPIO 25 para Tensão
#define DAC_CHANNEL_CURRENT DAC_CHAN_1 // GPIO 26 para Corrente
#define ADC_CHANNEL_VOLTAGE ADC_CHANNEL_0 // GPIO 36
#define ADC_CHANNEL_CURRENT ADC_CHANNEL_3 // GPIO 39
#define ADC_WIDTH ADC_BITWIDTH_12
#define ADC_ATTEN ADC_ATTEN_DB_12

SemaphoreHandle_t sensorMutex;
QueueHandle_t sensorQueue;
adc_oneshot_unit_handle_t adc_handle;
dac_cosine_handle_t dac_handle_voltage;
dac_cosine_handle_t dac_handle_current;

float energia_total = 0.0;
const float tarifa_kwh = 0.095;

void generate_voltage_wave(void) {
    dac_cosine_config_t dac_cfg_voltage = {
        .chan_id = DAC_CHANNEL_VOLTAGE,
        .freq_hz = 1000,
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
        .offset = 0,
        .phase = DAC_COSINE_PHASE_0,
        .atten = DAC_COSINE_ATTEN_DEFAULT,
        .flags.force_set_freq = true,
    };

    ESP_ERROR_CHECK(dac_cosine_new_channel(&dac_cfg_voltage, &dac_handle_voltage));
    ESP_ERROR_CHECK(dac_cosine_start(dac_handle_voltage));
}

void generate_current_wave(void) {
    dac_cosine_config_t dac_cfg_current = {
        .chan_id = DAC_CHANNEL_CURRENT,
        .freq_hz = 1000,
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
        .offset = 0,
        .phase = DAC_COSINE_PHASE_0,
        .atten = DAC_COSINE_ATTEN_DEFAULT,
        .flags.force_set_freq = true,
    };

    ESP_ERROR_CHECK(dac_cosine_new_channel(&dac_cfg_current, &dac_handle_current));
    ESP_ERROR_CHECK(dac_cosine_start(dac_handle_current));
}

void read_voltage_value(float *voltage) {
    int adc_val_voltage = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL_VOLTAGE, &adc_val_voltage));
    *voltage = (float)adc_val_voltage / 4095.0 * 220.0; // Assume 12-bit ADC and 0-220V range
}

void read_current_value(float *current) {
    int adc_val_current = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL_CURRENT, &adc_val_current));
    *current = (float)adc_val_current / 4095.0 * 15.0; // Assume 12-bit ADC and 0-15A range
}

void calcular_energia(float corrente, float tensao) {
    float potencia = corrente * tensao;

    // Calcula energia consumida em 5 segundos (5/3600 horas)
    float energia = potencia * (5.0 / 3600.0);
    energia_total += energia;

    float custo = energia_total * tarifa_kwh;

    ESP_LOGI(TAG, "Potência: %.2f W | Energia total: %.4f kWh | Custo: R$ %.2f",
             potencia, energia_total, custo);
}

// Função para salvar energia total no NVS
void salvar_energia_total() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_blob(handle, "energia_total", &energia_total, sizeof(energia_total));
        nvs_commit(handle);
        nvs_close(handle);
    } else {
        ESP_LOGE(TAG, "Erro ao abrir NVS para salvar energia total");
    }
}

// Função para carregar energia total do NVS
void carregar_energia_total() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        size_t required_size = sizeof(energia_total);
        nvs_get_blob(handle, "energia_total", &energia_total, &required_size);
        nvs_close(handle);
    } else {
        ESP_LOGE(TAG, "Erro ao abrir NVS para carregar energia total");
    }
}

// Tarefa 1: Gerar e enviar dados de sensores para a fila
void simular_tarefa(void *pvParameters) {
    float dados[2];

    // Protege leituras com semaforo
    xSemaphoreTake(sensorMutex, portMAX_DELAY);
    read_current_value(&dados[0]); 
    read_voltage_value(&dados[1]);   
    xSemaphoreGive(sensorMutex);

    // Envia os dados para a fila
    if (xQueueSend(sensorQueue, &dados, portMAX_DELAY) == pdPASS) {
        ESP_LOGI(TAG, "Dados enviados para a fila: Corrente = %.2f A, Tensão = %.2f V",
                 dados[0], dados[1]);
    }

    vTaskDelete(NULL); // Tarefa executada uma vez
}

// Tarefa 2: Processar dados da fila
void processar_tarefa(void *pvParameters) {
    float dados[2];
    if (xQueueReceive(sensorQueue, &dados, pdMS_TO_TICKS(1000))) {
        float corrente = dados[0];
        float tensao = dados[1];

        calcular_energia(corrente, tensao);
        salvar_energia_total();
    }

    vTaskDelete(NULL); // Tarefa executada uma vez
}

// Tarefa 3: Gerenciar exibição e modo de economia de energia
void gerenciar_tarefa(void *pvParameters) {
    // Carrega a energia total do NVS para manter a persistência após o deep sleep
    carregar_energia_total();
    
    ESP_LOGI(TAG, "Entrando em deep sleep por 5 segundos...");
    esp_sleep_enable_timer_wakeup(TEMPO_DEEP_SLEEP);
    esp_deep_sleep_start();
}

void app_main() {
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_LOGE(TAG, "Erro ao inicializar NVS");
        return;
    }

    // Apaga a partição 'storage'
    /*const esp_partition_t *nvs_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
    if (nvs_partition != NULL) {
        esp_err_t err = esp_partition_erase_range(nvs_partition, 0, nvs_partition->size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Erro ao apagar a partição NVS: %s", esp_err_to_name(err));
            return;
        }
        ESP_LOGI(TAG, "Partição NVS apagada com sucesso");
    }*/
    
    sensorMutex = xSemaphoreCreateMutex();
    if (sensorMutex == NULL) {
        ESP_LOGE(TAG, "Falha ao criar mutex");
        return;
    }

    sensorQueue = xQueueCreate(10, sizeof(float[2]));
    if (sensorQueue == NULL) {
        ESP_LOGE(TAG, "Falha ao criar fila");
        return;
    }

    carregar_energia_total();

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG, "ESP acordou do deep sleep");
    } else {
        ESP_LOGI(TAG, "Inicializando o sistema pela primeira vez");
        energia_total = 0.0; // Inicializa energia total como 0
    }

    // Inicializa DACs
    generate_voltage_wave();
    generate_current_wave();

    // Inicializa ADC
    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = false,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_cfg, &adc_handle));
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_WIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_VOLTAGE, &chan_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_CURRENT, &chan_cfg));

    // Cria tarefas
    xTaskCreate(simular_tarefa, "SimularTarefa", 2048, NULL, 5, NULL);
    xTaskCreate(processar_tarefa, "ProcessarTarefa", 2048, NULL, 5, NULL);
    xTaskCreate(gerenciar_tarefa, "GerenciarTarefa", 2048, NULL, 5, NULL);
}
