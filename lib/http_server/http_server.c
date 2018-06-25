#include <http_server.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

#include <string.h>

char *header  = "HTTP/1.0 200\nContent-type: text/html; charset=\"utf-8\"\n\n";
char *http_server_default_website = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><title>ESP WEBSITE</title></head><body>This is the default ESP Website! Add a callback to customize the response!</body></html>";
char *complete_response;

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
    request->body = (char *) malloc(1);
    *(request->body) = '\0';
    if (request->method == HTTP_POST) {
        do { start++; /*printf("%d ", *start);*/ } while ((!(start[0] == '\n' && start[1] == '\n') && !(start[0]==13 && start[1] == 10 && start[2] == 13 && start[3] == 10)) && start[0] != '\0');//( (!(start[0] == 'n' && start[1] == 't' && start[2] == '-' && start[3] == 'L')) && (start <= data + strlen(data)));
        if (start < (data + strlen(data))) {
            do { start++; } while (*start <= 13);
            // printf("\nRequest has body!\n");
            end = data + strlen(data);//start;

            len = end - start;

            // printf("Length: %d\nContent: %s\n", len, start);
            request->body = (char *) malloc(len+1);
            bzero(request->body, len+1);
            memcpy(request->body, start, len);
        }
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
    } else {
        printf("Body: null\n\n");
    }

    // printf("----------");
}

bool str_cmp(char *a, char *b) {
    // printf("str_cmp: %s ?? %s", a, b);
    uint16_t len = strlen(a);
    if (strlen(b) < len) {
        len = strlen(b);
    }
    for (uint16_t i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            // printf(" -> FALSE\n");
            return false;
        }
    }
    // printf(" -> TRUE\n");
    return true;
} 

char *scan_url_encoded(char *data, char *key) {
    char *url_key = malloc(strlen(key) + 2);
    bzero(url_key, strlen(key) + 2);
    sprintf(url_key, "%s=", key);
    while (!str_cmp(data, url_key)) { data++; }
    if (strlen(data) == 0) {
        printf("COUND NOT FIND VALUE FOR KEY: %s\n", key);
        char *ret = malloc(1);
        *ret = 0;
        return ret;
    }
    data += strlen(url_key);
    uint8_t i = 0;
    while (*(data+i) != '&' && *(data+i) != '\n' && *(data+i) != '\r' && *(data+i) != '\0') { i++; }

    char *ret = (char *) malloc(i+1);
    bzero(ret, i+1);
    // memcpy(ret, data, i);
    for (uint8_t j = 0; j < i; j++) {
        if (data[j] == '%') {
            char hex[] = {(++data)[j], (++data)[j], '\0'};
            ret[j] = (uint8_t) strtol(hex, NULL, 16);
            printf("%s -> %c\n", hex, ret[j]);
        } else if (data[j] == '+') {
            ret[j] = ' ';
        } else {
            ret[j] = data[j];
        }
    }

    return ret;
}

void default_client_callback(struct http_request *request, char *buffer) {
    strcpy(buffer, http_server_default_website);
}

void (*http_server_client_callback)(struct http_request *request, char *buffer) = &default_client_callback;
void http_server_set_client_callback(void (*callback)(struct http_request *request, char *buffer)) {
    http_server_client_callback = callback;
}

void http_client_task(void *pvParameters)
{
    char *TAG = "http_client_task";

    int fd = (int) pvParameters;
    int buffer_size = 2000;
    char *buffer = (char *)malloc(buffer_size);
    int n = 0;

    while (true) {
        bzero(buffer, buffer_size);
        n = read(fd, buffer, buffer_size);
        // printf("Received: \n%s-----\n", buffer);
        if (n < 0) {
            break;
        }

        struct http_request request;
        make_http_request(&request, buffer);
        http_request_print(&request);

        char *body = (char *) malloc(1024);
        bzero(body, 1024);
        http_server_client_callback(&request, body);
        
        char *response = (char *) malloc(strlen(body) + strlen(header) + 1);
        bzero(response, strlen(body) + strlen(header) + 1);
        snprintf(response, strlen(header) + strlen(body) + 1, "%s%s", header, body);
        
        write(fd, response, strlen(response));

        free(response);
        response = NULL;

        free(body);
        body = NULL;

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

void http_server_start(void) {
    xTaskCreate(http_server_task, "http_server_task", 4096, NULL, 15, NULL);
}

void http_server_stop(void) {
    
}