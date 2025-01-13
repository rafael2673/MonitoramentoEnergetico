#include "sensors.h"

#define ADC_ATTEN ADC_ATTEN_DB_12
#define ADC_WIDTH ADC_BITWIDTH_12

sensor_t sensores[MAX_SENSORS];
int num_sensores = 0;
const float tarifa_kwh = 0.095;
adc_oneshot_unit_handle_t adc_handle; // Adicionar esta linha

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

void calcular_energia(sensor_t* sensor) {
    float potencia = sensor->corrente * sensor->tensao;

    // Calcula energia consumida em 5 segundos (5/3600 horas)
    float energia = potencia * (5.0 / 3600.0);
    sensor->energia_total += energia;

    sensor->custo = sensor->energia_total * tarifa_kwh;

    ESP_LOGI(TAG, "Potência: %.2f W | Energia total: %.4f kWh | Custo: R$ %.2f",
             potencia, sensor->energia_total, sensor->custo);
}

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
