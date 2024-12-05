#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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

// Mutex para proteger leituras de sensores
SemaphoreHandle_t sensorMutex;

// Variáveis globais para energia total e custo
float energia_total = 0.0;
const float tarifa_kwh = 0.95; // Tarifa simulada em reais por kWh

// Função para simular leitura de corrente
float simular_corrente() {
    return 0.05 + ((float)(rand() % 100) / 10); // Simula entre 0.05A e 15A
}

// Função para simular leitura de tensão
float simular_tensao() {
    return 210 + ((float)(rand() % 20)); // Simula entre 210V e 230V
}

// Função para calcular energia e custo
void calcular_energia(float corrente, float tensao) {
    // Calcula potência instantânea
    float potencia = corrente * tensao;

    // Calcula energia consumida em 5 segundos (5/3600 horas)
    float energia = potencia * (5.0 / 3600.0);

    // Atualiza energia total
    energia_total += energia;

    // Calcula o custo total
    float custo = energia_total * tarifa_kwh;

    // Exibe os resultados
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

// Função para medir corrente e tensão
void medir_tarefa(void *pvParameters) {
    float corrente, tensao;

    // Inicializa a seed para a geração de números aleatórios com base no tempo atual
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand(tv.tv_usec);

    // Simula medições protegidas por mutex
    xSemaphoreTake(sensorMutex, portMAX_DELAY);
    corrente = simular_corrente();
    tensao = simular_tensao();
    xSemaphoreGive(sensorMutex);

    ESP_LOGI(TAG, "Corrente: %.2f A | Tensão: %.2f V", corrente, tensao);

    // Calcula energia e custo
    calcular_energia(corrente, tensao);

    // Salva energia total no NVS
    salvar_energia_total();

    // Configura deep sleep
    ESP_LOGI(TAG, "Entrando em deep sleep por 5 segundos...");
    esp_sleep_enable_timer_wakeup(TEMPO_DEEP_SLEEP);
    esp_deep_sleep_start();
}

// Função principal
void app_main() {
    // Inicializa NVS
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_LOGE(TAG, "Erro ao inicializar NVS");
        return;
    }

    // Inicializa mutex
    sensorMutex = xSemaphoreCreateMutex();
    if (sensorMutex == NULL) {
        ESP_LOGE(TAG, "Falha ao criar o mutex");
        return;
    }

    // Carrega energia total do NVS
    carregar_energia_total();

    // Detecta se o ESP acordou do deep sleep
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG, "ESP acordou do deep sleep");
    } else {
        ESP_LOGI(TAG, "Inicializando o sistema pela primeira vez");
        energia_total = 0.0; // Inicializa energia total como 0 na primeira execução
    }

    // Cria a tarefa para medições
    xTaskCreate(medir_tarefa, "MedirTarefa", 2048, NULL, 5, NULL);
}
