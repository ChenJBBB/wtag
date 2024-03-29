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
TaskHandle_t wifiScanTaskHandler;
TaskHandle_t wifiTryConnectTaskHandler;

// 队列句柄
QueueHandle_t apInfoQueueHandler; // 无密码AP信息队列

// 信号量
SemaphoreHandle_t apNoPswSephHandler;
static const char *TAG = "MAIN";

static void wifi_scan(void)
{
    uint16_t maxScanNumber = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t apInfo[DEFAULT_SCAN_LIST_SIZE]; // AP信息记录的结构体
    uint16_t apCount = 0, noPswApCount = 0;
    memset(apInfo, 0, sizeof(apInfo));
    esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&maxScanNumber, apInfo));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&apCount));
    ESP_LOGI(TAG, "Total APs scanned = %u", apCount);
    for (int i = 0; (i < DEFAULT_SCAN_LIST_SIZE) && (i < apCount); i++)
    {
        if (apInfo[i].authmode == WIFI_AUTH_OPEN) // 查找没有加密的AP
        {
            ESP_LOGI(TAG, "Find The AP no psw ");
            ESP_LOGI(TAG, "SSID \t\t%s", apInfo[i].ssid);
            ESP_LOGI(TAG, "RSSI \t\t%d", apInfo[i].rssi);
            ESP_LOGI(TAG, "Channel \t\t%d\n", apInfo[i].primary);
            xQueueSend(apInfoQueueHandler, &apInfo[i], portMAX_DELAY);
            noPswApCount++;
        }
    }
    if (noPswApCount) // 存在未加密的AP
    {
        ESP_ERROR_CHECK(esp_wifi_scan_stop());
        ESP_ERROR_CHECK(esp_wifi_stop());
        for (size_t i = 0; i < noPswApCount; i++) // 根据获取到的AP数量传递信号量给连接任务
        {
            xSemaphoreGive(apNoPswSephHandler);
        }
        noPswApCount = 0;
        vTaskSuspend(wifiScanTaskHandler); // 存在无密码AP，挂起扫描任务
    }
}

void vTaskWifiScan(void *pvParameters)
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    apInfoQueueHandler = xQueueCreate(10, sizeof(wifi_ap_record_t));
    if (apInfoQueueHandler == NULL)
    {
        vTaskDelete(NULL);
    }
    while (1)
    {
        wifi_scan();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void vTaskTryConnect(void *pvParameters)
{
    wifi_ap_record_t apInfo;
    while (1)
    {
        while (xSemaphoreTake(apNoPswSephHandler, (200 / portTICK_PERIOD_MS))) // 等待无密码的AP信号量到来
        {
            if (pdFALSE == xQueueReceive(apInfoQueueHandler, &apInfo, (200 / portTICK_PERIOD_MS)))
            {
                ESP_LOGE(TAG, "xQueueReceive apInfo Failed");
            }
            ESP_LOGI(TAG, "Recive ap no psw SSID = %s", apInfo.ssid);
            if (tryConnect(apInfo) != ESP_OK) // 连接失败
            {
                ESP_LOGI(TAG, "Connect  %s failed try next", apInfo.ssid);
            }
            else
            {
                ESP_LOGI(TAG, "AP Connect start judge whether to surf the Internet");
                while (1)
                {
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }
            }
        }
        wifiScanTaskHandler = xTaskGetHandle("wifi scan");
        if ((int)eTaskGetState(wifiScanTaskHandler) == (int)eSuspended) // 无密码AP消耗完，但扫描任务还是挂起
        {
            vTaskResume(wifiScanTaskHandler);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    // Wi-Fi/LwIP 初始化阶段
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    apNoPswSephHandler = xSemaphoreCreateCounting(DEFAULT_SCAN_LIST_SIZE, 0); // 计数信号量 表征没有密码的AP数量
    if (apNoPswSephHandler == NULL)
    {
        ESP_LOGE(TAG, "apNoPswSephHandler Creat falid");
    }
    xTaskCreate(vTaskWifiScan, "wifi scan", 16384, NULL, 10 | portPRIVILEGE_BIT, &wifiScanTaskHandler);
    wifiScanTaskHandler = xTaskGetHandle("wifi scan");
    xTaskCreate(vTaskTryConnect, "try connect", 16384, NULL, 12 | portPRIVILEGE_BIT, &wifiTryConnectTaskHandler);
    wifiTryConnectTaskHandler = xTaskGetHandle("try connect");
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
