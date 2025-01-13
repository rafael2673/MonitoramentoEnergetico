#ifndef SENSORS_H
#define SENSORS_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/dac_cosine.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"

#define MAX_SENSORS 8
#define TAG "MONITORAMENTO"
#define ADC_ATTEN ADC_ATTEN_DB_12
#define ADC_WIDTH ADC_BITWIDTH_12

typedef struct {
    const char* device_id;
    int adc_channel_voltage;
    int adc_channel_current;
    float corrente;
    float tensao;
    float energia_total;
    float custo;
} sensor_t;

extern sensor_t sensores[MAX_SENSORS];
extern int num_sensores;
extern SemaphoreHandle_t sensorMutex;
extern QueueHandle_t sensorQueue;
extern adc_oneshot_unit_handle_t adc_handle; // Adicionar esta linha

void adicionar_sensor(const char* device_id, int adc_channel_voltage, int adc_channel_current);
void generate_voltage_wave(dac_channel_t channel);
void generate_current_wave(dac_channel_t channel);
void setup_adc_channels();
void carregar_energia_total(sensor_t* sensor);
void read_voltage_value(int adc_channel, float *voltage);
void read_current_value(int adc_channel, float *current);
void calcular_energia(sensor_t* sensor);
void salvar_energia_total(sensor_t* sensor);

#endif // SENSORS_H
