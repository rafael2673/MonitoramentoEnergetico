#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>

// Definições
#define TEMPO_DEEP_SLEEP 5000000 // 5 segundos (em microsegundos)
#define TAG "MONITORAMENTO"

SemaphoreHandle_t sensorMutex;
QueueHandle_t sensorQueue;

float energia_total = 0.0;
const float tarifa_kwh = 0.95;


float simular_corrente() {
    return 0.05 + ((float)(rand() % 1500) / 100); // Simula entre 0.05A e 15A
}


float simular_tensao() {
    return 127 + ((float)(rand() % 94)); // Simula entre 127V até 220V
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

// Tarefa 1: Simular sensores e enviar para a fila
void simular_tarefa(void *pvParameters) {
    float dados[2];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand(tv.tv_usec); // Inicializa seed para números aleatórios

    // Protege leituras com mutex
    xSemaphoreTake(sensorMutex, portMAX_DELAY);
    dados[0] = simular_corrente(); 
    dados[1] = simular_tensao();   
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
    // ESP_LOGI(TAG, "Energia acumulada até agora: %.4f kWh", energia_total);

    // Configura deep sleep
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

    
    xTaskCreate(simular_tarefa, "SimularTarefa", 2048, NULL, 5, NULL);
    xTaskCreate(processar_tarefa, "ProcessarTarefa", 2048, NULL, 5, NULL);
    xTaskCreate(gerenciar_tarefa, "GerenciarTarefa", 2048, NULL, 5, NULL);
}
