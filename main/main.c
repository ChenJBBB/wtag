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
#include "tryConnect.h"

// 任务句柄
TaskHandle_t wifiScanTaskHandler = NULL;
;
TaskHandle_t wifiTryConnectTaskHandler = NULL;
;
// 队列句柄
QueueHandle_t apInfoQueueHandler = NULL;
;
// 定时器句柄
TimerHandle_t wdogTimerHandler = NULL;
;
// 二值信号量
SemaphoreHandle_t scanTask2TryTaskSephHandler = NULL;
static const char *TAG = "MAIN";

static void wifi_scan(void)
{
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t apInfo[DEFAULT_SCAN_LIST_SIZE]; // AP信息记录的结构体
    uint16_t apCount = 0, noPswApCount = 0;
    memset(apInfo, 0, sizeof(apInfo));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
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
        }
        if (noPswApCount)
        {
            ESP_ERROR_CHECK(esp_wifi_scan_stop());
            ESP_ERROR_CHECK(esp_wifi_stop());
            xSemaphoreGive(scanTask2TryTaskSephHandler); // 释放二值信号量
            vTaskSuspend(wifiScanTaskHandler);           //存在无密码AP，挂起扫描任务
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
        xSemaphoreTake(scanTask2TryTaskSephHandler, portMAX_DELAY);       // 等待二值信号量
        while (xQueueReceive(apInfoQueueHandler, &apInfo, portMAX_DELAY)) //阻塞等待
        {
            ESP_LOGI(TAG, "recive ap no psw %s", apInfo.ssid);
            if (!tryConnect(apInfo)) //连接失败
            {
                continue;
            }
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
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta(); //官方API 初始化为STA模式
    assert(sta_netif);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wdogTimerHandler = xTimerCreate("wdogTimer",
                                    (TickType_t)pdMS_TO_TICKS(300), // 200ms
                                    (UBaseType_t)pdTRUE,
                                    (void *)1,
                                    (TimerCallbackFunction_t)wdogTimerCallback);
    xTimerStart(wdogTimerHandler, 0);
    scanTask2TryTaskSephHandler = xSemaphoreCreateBinary(); //二值信号量创建
    if (scanTask2TryTaskSephHandler == NULL)
    {
        ESP_LOGE(TAG, "scanTask2TryTaskSephHandler Creat falid");
    }

    xTaskCreate(vTaskWifiScan, "wifi scan", 8192, NULL, 1, wifiScanTaskHandler);
    xTaskCreate(vTaskTryConnect, "try connect", 8192, NULL, 2, wifiTryConnectTaskHandler);
    vTaskStartScheduler();
}
