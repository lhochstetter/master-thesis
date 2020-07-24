#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_event_loop.h"

#include "driver/gpio.h"

#include "button.h"

EventGroupHandle_t g_button_event_group;
const int OTA_BIT = BIT0;

static xQueueHandle gpio_evt_queue = NULL;

static void IRAM_ATTR button_isr_handler(void *arg)
{
	uint32_t gpio_num = (uint32_t) arg;
	xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void button_handler(void *arg)
{
	uint32_t io_num;
	for (;;) {
		if (!xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
			continue;
#if 0
		printf("GPIO[%d] intr, val: %d\n", io_num,
		       gpio_get_level(io_num));
#endif
		xEventGroupSetBits(g_button_event_group, OTA_BIT);
	}
}

void button_init()
{
	gpio_set_direction(EXAMPLE_BUTTON, GPIO_MODE_INPUT);
	gpio_set_pull_mode(EXAMPLE_BUTTON, GPIO_PULLUP_ONLY);
	gpio_set_intr_type(EXAMPLE_BUTTON, GPIO_PIN_INTR_POSEDGE);

	/*create a queue to handle gpio event from isr */
	gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
	g_button_event_group = xEventGroupCreate();

	/* start gpio task */
	xTaskCreate(&button_handler, "button_handler", 2048, NULL,
		    10, NULL);

	/* install gpio isr service */
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

	/* hook isr handler for specific gpio pin */
	gpio_isr_handler_add(EXAMPLE_BUTTON,
			     button_isr_handler, (void *) EXAMPLE_BUTTON);
}
