#ifndef _TRYCONNECT_H_
#define _TRYCONNECT_H_

#include <string.h>
#include <FreeRTOSConfig.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "config.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

esp_err_t tryConnect(wifi_ap_record_t apInfo);

#endif //_TRYCONNECT_H_