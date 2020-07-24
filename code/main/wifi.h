#ifndef WIFI_H
#define WIFI_H

#include "freertos/event_groups.h"

extern EventGroupHandle_t g_wifi_event_group;

extern const int CONNECTED_BIT;

void wifi_init();

#endif
