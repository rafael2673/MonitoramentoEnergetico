#ifndef TASKS_H
#define TASKS_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensors.h"
#define TEMPO_DEEP_SLEEP 5000000 // 5 segundos (em microsegundos)

void simular_tarefa(void *pvParameters);
void processar_tarefa(void *pvParameters);
void enviar_dados_mqtt_tarefa(void *pvParameters);
void gerenciar_tarefa(void *pvParameters);

#endif // TASKS_H
