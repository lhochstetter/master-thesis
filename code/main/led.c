#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_event_loop.h"

#include "driver/gpio.h"

#include "led.h"

static bool m_blink;

static void led_blink_task(void *pvParameter)
{
	int level = 0;

	gpio_set_direction(EXAMPLE_LED, GPIO_MODE_OUTPUT);

	while (m_blink) {
		gpio_set_level(EXAMPLE_LED, level);
		level = !level;
		vTaskDelay(500 / portTICK_PERIOD_MS);
	}

	gpio_set_level(EXAMPLE_LED, 1);
}


void led_stop()
{
	m_blink = false;
}

void led_init() {
	m_blink = true;

	xTaskCreate(&led_blink_task, "led_blink_task", 2048, NULL,
		    5, NULL);
}
