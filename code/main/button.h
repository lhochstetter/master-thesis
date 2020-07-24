#ifndef BUTTON_H
#define BUTTON_H

#define EXAMPLE_BUTTON        GPIO_NUM_18
#define ESP_INTR_FLAG_DEFAULT 0

extern EventGroupHandle_t g_button_event_group;

extern const int OTA_BIT;

void button_init();

#endif
