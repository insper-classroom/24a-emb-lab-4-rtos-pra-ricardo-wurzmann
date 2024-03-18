#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "ssd1306.h"
#include "gfx.h"
#include <stdio.h>

const uint TRIG_PIN = 12;
const uint ECHO_PIN = 13;
const uint MAX_DISTANCE = 150; // Máxima distância em cm que queremos medir

// Definições para o display OLED
ssd1306_t disp;
#define OLED_WIDTH  128
#define OLED_HEIGHT 32

// Filas e semáforos para sincronização
QueueHandle_t xQueueDistance;
SemaphoreHandle_t xSemaphoreTrigger;

void pin_callback(uint gpio, uint32_t events) {
    static uint64_t start_time = 0;
    uint64_t time_diff, end_time;

    if (events & GPIO_IRQ_EDGE_RISE) {
        start_time = to_us_since_boot(get_absolute_time());
    } else if (events & GPIO_IRQ_EDGE_FALL) {
        end_time = to_us_since_boot(get_absolute_time());
        time_diff = end_time - start_time;

        // Verifica se o tempo de resposta está dentro de um intervalo válido
        if (time_diff < MAX_DISTANCE * 58) {
            xQueueSendFromISR(xQueueDistance, &time_diff, NULL);
        } else {
            time_diff = UINT64_MAX;
            xQueueSendFromISR(xQueueDistance, &time_diff, NULL);
        }
    }
}

void display_task(void *pvParameters) {
    uint64_t time_elapsed;
    float distance = 0.0f;
    char buffer[32]; // Adicione esta linha para declarar o buffer
    bool displayError = false;

    ssd1306_init();
    gfx_init(&disp, OLED_WIDTH, OLED_HEIGHT);

    while (1) {
        if (xSemaphoreTake(xSemaphoreTrigger, portMAX_DELAY)) {
            if (xQueueReceive(xQueueDistance, &time_elapsed, portMAX_DELAY)) {
                gfx_clear_buffer(&disp);

                if (time_elapsed == UINT64_MAX) {
                    snprintf(buffer, sizeof(buffer), "Sensor falhou");
                    displayError = true;
                } else {
                    distance = (float)time_elapsed * 0.0343 / 2.0;
                    snprintf(buffer, sizeof(buffer), "Dist: %.2f cm", distance);
                    displayError = false;
                }

                gfx_draw_string(&disp, 0, 0, 1, buffer);

                if (!displayError) {
                    // Desenhar barra de distância
                    int bar_length = (int)((distance / MAX_DISTANCE) * (OLED_WIDTH - 1));
                    gfx_draw_line(&disp, 0, OLED_HEIGHT - 10, bar_length, OLED_HEIGHT - 10);
                }

                gfx_show(&disp);
            }
        }
    }
}


void trigger_task(void *pvParameters) {
    while (1) {
        gpio_put(TRIG_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(10)); // Trigger pulse for 10 us
        gpio_put(TRIG_PIN, 0);
        xSemaphoreGive(xSemaphoreTrigger);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay de 1 segundo antes do próximo ciclo
    }
}

int main(void) {
    stdio_init_all();
    gpio_init(TRIG_PIN);
    gpio_set_dir(TRIG_PIN, GPIO_OUT);
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &pin_callback);

    xQueueDistance = xQueueCreate(10, sizeof(uint64_t));
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    xTaskCreate(trigger_task, "Trigger Task", 256, NULL, 1, NULL);
    xTaskCreate(display_task, "Display Task", 256, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1) {}
    return 0;
}
