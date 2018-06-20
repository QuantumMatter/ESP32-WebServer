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

char *header  = "HTTP/1.0 200\nContent-type: text/html; charset=\"utf-8\"\n\n";
char *website = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><title>ESP WEBSITE</title></head><body><form action=\"/update.html\" method=\"POST\">SSID:<br><input type=\"text\" name=\"ssid\"/><br>Password:<br><input type=\"password\" name=\"psk\"/><br><input type=\"submit\" value=\"Submit\"/></form></body></html>";
char *complete_response;

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

enum http_method {
    HTTP_GET = 1,
    HTTP_POST = 2,
    HTTP_ERROR = -1
};

struct http_request {
    enum http_method method;
    char *host;
    char *path;
    char *body;
} http_request_t;

void make_http_request(struct http_request *request, char *data) {
    // printf("Searching for request method...\n");
    switch (data[0]) {
        case 'G':
            request->method = HTTP_GET;
            break;
        case 'P':
            request->method = HTTP_POST;
            break;
        default:
            request->method = HTTP_ERROR;
            break;
    }
    // printf("Searching for path...\n");
    char *start = data;
    do { start++; } while (*start != '/');
    char *end = start;
    do { end++; } while (*end != ' ');

    // printf("Loading path component...\n");
    size_t len = (end - start) + 1;
    request->path = (char *) malloc(len);
    bzero(request->path, len);
    memcpy(request->path, start, len-1);

    // printf("Searching for host line...\n");
    do { start++; } while (!(start[0] == 'H' && start[1] == 'o' && start[2] == 's' && start[3] == 't'));
    start += 5;
    // printf("Searching for end of host line...\n");
    end = ++start;
    do { end++; } while (*end != '\n');
    
    // printf("Loading host string...\n");

    len = (end - start) + 1;
    request->host = (char *) malloc(len);
    bzero(request->host, len);
    memcpy(request->host, start, len-1);

    // printf("Looking for content-length...\n");
    do { start++; /*printf("%c", *start);*/ } while ( (!(start[0] == 'n' && start[1] == 't' && start[2] == '-' && start[3] == 'l')) && (start <= data + strlen(data)));
    if (start < (data + strlen(data))) {
        // printf("\nRequest has body!\n");
        start += 11;
        end = start;
        do { end++; } while ( *end != '\n' );
        len = end-start + 1;
        char content_length[len+1];
        bzero(content_length, len+1);
        memcpy(content_length, start, len-1);
        // printf("Content Length Str: %s\n", content_length);

        len = atoi(content_length);
        // printf("Content Length Int: %d\n", len);

        request->body = (char *) malloc(len+1);
        bzero(request->body, len+1);
        memcpy(request->body, data + (strlen(data) - len), len);
        // printf("Found Body! \n---\n%s\n---\n", request->body);

        // printf("Found body data!\n%s\n---\n", start);
        // len = (data + strlen(data)) - start + 1;
        // printf("Body length: %d\n", len);
        // request->body = (char *) malloc(len);
        // bzero(request->body, len);
        // memcpy(request->body, start, len-1);
    } else {
        // printf("Request does not have a body\n");
        request->body = (char *) malloc(1);
        *(request->body) = '\0';
    }
}

void http_request_print(struct http_request *request)
{
    printf("Method: ");
    switch (request->method) {
        case HTTP_GET:
            printf("GET");
            break;
        case HTTP_POST:
            printf("POST"); 
            break;
        case HTTP_ERROR:
            printf("ERROR!");
            return;
    }
    printf("\n");

    printf("Host: %s\n", request->host);
    printf("Path: %s\n", request->path);
    if (request->method == HTTP_POST) {
        printf("Body: %s\n\n", request->body);
    }
}

void http_client_task(void *pvParameters)
{
    char *TAG = "http_client_task";
    ESP_LOGI(TAG, "New HTTP Request!");

    int fd = (int) pvParameters;
    int buffer_size = 2000;
    char *buffer = (char *)malloc(buffer_size);
    int n = 0;

    while (true) {
        bzero(buffer, buffer_size);
        n = read(fd, buffer, buffer_size);
        // printf("Received: \n%s\n\n", buffer);
        if (n < 0) {
            break;
        }
        write(fd, complete_response, strlen(complete_response));

        struct http_request request;
        // printf("Making request object!\n");
        make_http_request(&request, buffer);
        // printf("Printing request object!\n");
        http_request_print(&request);

        break;
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

    size_t sz = strlen(header) + strlen(website) + 5;
    complete_response = (char *) malloc(sz);
    bzero(complete_response, sz);
    snprintf(complete_response, sz, "%s%s", header, website);
    printf("\n\nHeader: %s\n", header);
    printf("Website: %s\n", website);
    printf("Web Response: %s\n", complete_response);
    printf("Size: %d\n\n", sz);
    //sprintf("%s%s", header, website);
    xTaskCreate(http_server_task, "http_server_task", 4096, NULL, 15, NULL);
}

