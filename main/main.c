/*
    This example shows how to scan for available set of APs.
*/
#include <string.h>
#include <FreeRTOSConfig.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "config.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"

// 任务句柄
TaskHandle_t wifiScanTaskHandler;
TaskHandle_t wifiTryConnectTaskHandler;
// 队列句柄
QueueHandle_t apInfoQueueHandler;
// 定时器句柄
TimerHandle_t wdogTimerHandler;
static const char *TAG = "MAIN";

/* Initialize Wi-Fi as sta and set scan method */
static void wifi_scan_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta(); //官方API 初始化为STA模式
    assert(sta_netif);
    //👆以上为netif的初始化过程
    //👇下面开始为WiFi的配置
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
}

static void wifi_scan(void)
{
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t apInfo[DEFAULT_SCAN_LIST_SIZE]; // AP信息记录的结构体
    uint16_t apCount = 0, noPswApCount = 0;
    memset(apInfo, 0, sizeof(apInfo));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, apInfo));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&apCount));
    ESP_LOGI(TAG, "Total APs scanned = %u", apCount);
    for (int i = 0; (i < DEFAULT_SCAN_LIST_SIZE) && (i < apCount); i++)
    {
        if (apInfo[i].authmode == WIFI_AUTH_OPEN)
        {
            ESP_LOGI(TAG, "Find The AP no psw ");
            ESP_LOGI(TAG, "SSID \t\t%s", apInfo[i].ssid);
            ESP_LOGI(TAG, "RSSI \t\t%d", apInfo[i].rssi);
            ESP_LOGI(TAG, "Channel \t\t%d\n", apInfo[i].primary);
            xQueueSend(apInfoQueueHandler, &apInfo[i], portMAX_DELAY);
            noPswApCount++;
        }
        if (noPswApCount)
        {
            vTaskSuspend(wifiScanTaskHandler); //存在无密码AP，挂起扫描任务
        }
    }
}

void vTaskWifiScan(void *pvParameters)
{
    apInfoQueueHandler = xQueueCreate(10, sizeof(wifi_ap_record_t));
    if (apInfoQueueHandler == NULL)
    {
        vTaskDelete(NULL);
    }
    while (1)
    {
        wifi_scan();
    }
}

void vTaskTryConnect(void *pvParameters)
{
    wifi_ap_record_t apInfo;
    while (1)
    {
        if (xQueueReceive(apInfoQueueHandler, &apInfo, portMAX_DELAY)) //阻塞等待
        {
            ESP_LOGI(TAG, "recive ap no psw %s", apInfo.ssid);
        }
    }
}

void wdogTimerCallback(TimerHandle_t xTimer) //定时喂狗任务
{
    esp_task_wdt_reset();
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    wifi_scan_init();
    wdogTimerHandler = xTimerCreate("wdogTimer",
                                    (TickType_t)pdMS_TO_TICKS(300), // 200ms
                                    (UBaseType_t)pdTRUE,
                                    (void *)1,
                                    (TimerCallbackFunction_t)wdogTimerCallback);
    xTimerStart(wdogTimerHandler, 0);
    xTaskCreate(vTaskWifiScan, "wifi scan", 8192, NULL, 1, wifiScanTaskHandler);
    xTaskCreate(vTaskTryConnect, "try connect", 8192, NULL, 2, wifiTryConnectTaskHandler);
    vTaskStartScheduler();
}
