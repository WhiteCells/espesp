#ifndef WIFI_STATION_H
#define WIFI_STATION_H

#include "esp_err.h"

esp_err_t wifi_station_connect(void);
esp_err_t wifi_station_run(void);

#endif
