#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"



#include <string.h>

#define WIFI_SSID       "ESP32_WIFI"
#define WIFI_PSK        "password1234"
#define WIFI_MAX_CONN   5

char *website = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><title>ESP WEBSITE</title></head><body>HELLO WORLD!</body></html>";

static EventGroupHandle_t wifi_event_group;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    const char *TAG = "EVENT_HANDLER";
    switch (event->event_id) {
        case SYSTEM_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "station:"MACSTR" join, AID=%d",
                    MAC2STR(event->event_info.sta_connected.mac),
                    event->event_info.sta_connected.aid);
            break;

        case SYSTEM_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "station:"MACSTR"leave, AID=%d",
                    MAC2STR(event->event_info.sta_disconnected.mac),
                    event->event_info.sta_disconnected.aid);
            break;

        default:
            break;
    }
    return ESP_OK;
}

void wifi_init_softap(void)
{
    wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(  esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    wifi_config_t config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PSK,
            .max_connection = WIFI_MAX_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_AP, &config) );
    ESP_ERROR_CHECK( esp_wifi_start() );

    ESP_LOGI("wifi_init_softap", "wifi_init_softap finished; SSID:%s password:%s",
             WIFI_SSID, WIFI_PSK);
}

void http_client_task(void *pvParameters)
{
    char *TAG = "http_client_task";
    ESP_LOGI(TAG, "Handling the new client!");

    int fd = (int) pvParameters;
    ESP_LOGI(TAG, "Client fd: %d", fd);
    int buffer_size = 2000;
    char *buffer = (char *)malloc(buffer_size);
    int n = 0;

    while (true) {
        bzero(buffer, buffer_size);
        n = read(fd, buffer, buffer_size);
        if (n < 0) {
            ESP_LOGI(TAG, "Client Disconnected!");
            break;
        }

        ESP_LOGI(TAG, "Received: %s", buffer);
        write(fd, website, strlen(website));
    }
    close(fd);
    vTaskDelete(NULL);
}

void http_server_task(void *pvParameters)
{
    const char *TAG = "http_server_task";
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        ESP_LOGE(TAG, "Could not create server!");
        return;
    }

    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        ESP_LOGE(TAG, "Could not set socket options!");
    }

    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(80);

    if (bind(fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        ESP_LOGE(TAG, "Could not bind socket!");
        return;
    }

    if (listen(fd, 5) < 0) {
        ESP_LOGE(TAG, "Could not listen on socket!");
        return;
    }

    esp_err_t err = ESP_OK;
    struct sockaddr_in client_addr;
    size_t client_len = sizeof(client_addr);
    ESP_LOGI(TAG, "Finished configuring socket!");
    do {
        int client = accept(fd, (struct sockaddr *) &client_addr, &client_len);
        ESP_LOGI(TAG, "A Client Connected: %d", client);
        if (xTaskCreate(http_client_task, "http_client_task", 8192, (void *)client, 14, NULL) == errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) {
            ESP_LOGE(TAG, "Could not create client task!");
        }

    } while (err == ESP_OK);
}

void app_main(void) 
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    wifi_init_softap();
    xTaskCreate(http_server_task, "http_server_task", 4096, NULL, 15, NULL);
}

