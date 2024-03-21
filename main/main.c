#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "ssd1306.h"
#include "gfx.h"

const uint TRIG_PIN = 12;
const uint ECHO_PIN = 13;
const uint MAX_DISTANCE = 150; // Máxima distância em cm

ssd1306_t disp;
#define OLED_WIDTH  128
#define OLED_HEIGHT 32

QueueHandle_t xQueueTime;
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
        xQueueSendFromISR(xQueueTime, &time_diff, NULL);
    }
}

void trigger_task(void *pvParameters) {
    while (1) {
        gpio_put(TRIG_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(10)); // Send trigger pulse for 10 microseconds
        gpio_put(TRIG_PIN, 0);
        xSemaphoreGive(xSemaphoreTrigger);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for 1 second before next pulse
    }
}

void echo_task(void *pvParameters) {
    uint64_t time_diff;
    float distance;

    while (1) {
        if (xQueueReceive(xQueueTime, &time_diff, portMAX_DELAY)) {
            if (time_diff < MAX_DISTANCE * 58) { // Check if distance is within range
                distance = (float)time_diff * 0.0343 / 2.0; // Convert time to distance
                xQueueSend(xQueueDistance, &distance, portMAX_DELAY);
            }
        }
    }
}

void oled_task(void *pvParameters) {
    float distance;
    char buffer[32];

    ssd1306_init();
    gfx_init(&disp, OLED_WIDTH, OLED_HEIGHT);

    while (1) {
        if (xSemaphoreTake(xSemaphoreTrigger, portMAX_DELAY)) {
            if (xQueueReceive(xQueueDistance, &distance, portMAX_DELAY)) {
                gfx_clear_buffer(&disp);
                snprintf(buffer, sizeof(buffer), "Dist: %.2f cm", distance);
                gfx_draw_string(&disp, 0, 0, 1, buffer);

                int bar_length = (int)((distance / MAX_DISTANCE) * OLED_WIDTH);
                gfx_draw_line(&disp, 0, OLED_HEIGHT - 10, bar_length, OLED_HEIGHT - 10);
                
                gfx_show(&disp);
            }
        }
    }
}

int main(void) {
    stdio_init_all();
    gpio_init(TRIG_PIN);
    gpio_set_dir(TRIG_PIN, GPIO_OUT);
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &pin_callback);

    xQueueTime = xQueueCreate(10, sizeof(uint64_t));
    xQueueDistance = xQueueCreate(10, sizeof(float));
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    xTaskCreate(trigger_task, "Trigger Task", 256, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo Task", 256, NULL, 1, NULL);
    xTaskCreate(oled_task, "Display Task", 256, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1) {}
    return 0;
}
