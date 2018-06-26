#include <http_requset.h>

#include <freertos/FreeRTOS.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

void make_request(http_request_t *request, char *url, http_method_t method)
{
    char *start;
    char *end;
    size_t sz;

    //Remove "http://" from url if it's there
    if (url[0] == 'h' && url[1] == 't' && url[2] == 't') {
        url += 7;
    }

    start = url;
    end = start;
    do { end++; } while ( *end != '/' && *end != '\0');

    sz = (end - start) + 1;
    request->host = (char *) malloc(((end - start) + 1));
    bzero(request->host, sz);
    memcpy(request->host, start, sz-1);

    sz = strlen(end) + 1;
    request->path = (char *) malloc(sz);
    bzero(request->path, sz);
    memcpy(request->path, end, sz - 1);

    request->method = method;

    request->headers = (char *) malloc(1);
    bzero(request->headers, 1);

    request->body = (char *) malloc(1);
    *(request->body) = 0;
}

void http_requset_add_header(http_request_t *request, char *key, char *value) {
    size_t sz = strlen(key) + strlen(value) + 4;
    char *header = (char *) malloc(sz);
    bzero(header, sz);
    sprintf(header, "\n%s: %s", key, value);

    if (strlen(request->headers) == 0) {
        request->headers = (char *) malloc(512);
        bzero(request->headers, 512);
    }

    memcpy(request->headers + strlen(request->headers), header, strlen(header));
}

void http_request_get_header(http_request_t *request, char *key, char *value) {
    char *headers = request->headers;
    while (!str_cmp(headers, key) && *headers != 0) {
        headers++;
    }
    if (*headers == 0) {
        printf("Could not find header for: %s\n", key);
        return;
    }

    while (*headers != ':') { headers++; }
    char *end = headers;
    while (*end != '\r' && *end != '\n' && *end != 0) { end++; }
    
    size_t sz = (end - headers) + 1;
    memcpy(value, headers, sz);
}

char *ss_cpy(char *destination, char *source) {
    uint8_t i = 0;
    while (source[i] != '\0') {
        destination[i] = source[i];
        i++;
    }
    return destination += i;
}

void make_request_text(http_request_t *request, char *buffer, size_t sz) {
    bzero(buffer, sz);

    switch (request->method) {
        case HTTP_GET:
            buffer = ss_cpy(buffer, "GET");
            break;
        
        case HTTP_POST:
            buffer = ss_cpy(buffer, "POST");
            break;

        default:
            printf("CANNOT CREATE REQUEST TEST; UNKNOWN METHOD\n");
            return;
    }
    buffer = ss_cpy(buffer, " ");
    buffer = ss_cpy(buffer, request->path);
    buffer = ss_cpy(buffer, " HTTP/1.0\r\nHost: ");
    buffer = ss_cpy(buffer, request->host);
    buffer = ss_cpy(buffer, "\r\nUser-Agent: esp-idf/1.0 esp32");

    if (request->method == HTTP_POST) {
        // printf("Adding Content-Length Header; ");
        char length[4];
        itoa(strlen(request->body), length, 10);
        // printf("%s\n", length);
        http_requset_add_header(request, "Content-Length", length);
        // printf("Finished adding length header!\n");
    }

    if (strlen(request->headers) > 0) {
        buffer = ss_cpy(buffer, request->headers);
    }

    buffer = ss_cpy(buffer, "\r\n\r\n");

    if (request->method == HTTP_POST) {
        buffer = ss_cpy(buffer, request->body);
    }
}

void http_request_default_fail(http_request_t *request, http_request_fail_t error_code) {
    switch (error_code) {
        case HTTP_REQUEST_FAIL_DNS:
            printf("DNS LOOKUP FAILED. COULD NOT SEND REQUEST.\n");
            break;

        case HTTP_REQUEST_FAIL_SOCKET:
            printf("Could not create socket!\n");
            break;

        case HTTP_REQUEST_FAIL_CONNECT:
            printf("Could not connect to server!\n");
            break;

        case HTTP_REQUEST_FAIL_SEND:
            printf("Could not send request!\n");
            break;

        case HTTP_REQUEST_FAIL_SET_TIMEOUT:
            printf("Could not set receive timeout!\n");
            break;

        default:
            printf("UNKNOWN HTTP ERROR!\n");
            break;
    }
}

void http_request_default_success(http_request_t *request, char *data) {
    printf("Received:\n%s------\n", data);
}

static http_request_callback_t default_callback = {
    .fail = &http_request_default_fail,
    .success = &http_request_default_success,
};
static http_request_callback_t *request_callback = &default_callback;

void http_request_send(http_request_t *request) {
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *serv;
    struct in_addr *addr;
    int s, r;

    int err = getaddrinfo(request->host, "80", &hints, &serv);

    if (err != 0 || serv == NULL) {
        request_callback->fail(request, HTTP_REQUEST_FAIL_DNS);
        return;
    }

    addr = &((struct sockaddr_in *) serv->ai_addr)->sin_addr;
    // printf("DNS Lookup successful; IP=%s\n", inet_ntoa(*addr));

    s = socket(serv->ai_family, serv->ai_socktype, 0);
    if (s < 0) {
        freeaddrinfo(serv);
        request_callback->fail(request, HTTP_REQUEST_FAIL_SOCKET);
        return;
    }
    // printf("... Allocated Socket\n");

    if (connect(s, serv->ai_addr, serv->ai_addrlen) < 0) {
        request_callback->fail(request, HTTP_REQUEST_FAIL_CONNECT);
        close(s);
        freeaddrinfo(serv);
        return;
    }
    // printf("... Connected!\n");

    freeaddrinfo(serv);

    char *data = (char *) malloc(1024);
    bzero(data, 1024);
    make_request_text(request, data, 1024);

    if (write(s, data, strlen(data)) < 0) {
        request_callback->fail(request, HTTP_REQUEST_FAIL_SEND);
        close(s);
        return;
    }
    // printf("... Sent request!\n");

    struct timeval receive_timeout;
    receive_timeout.tv_sec = 5;
    receive_timeout.tv_usec = 0;
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout)) < 0) {
        request_callback->fail(request, HTTP_REQUEST_FAIL_SET_TIMEOUT);
        return;
    }
    // printf("... Set timeout\n");

    do {
        bzero(data, 1024);
        r = read(s, data, 1023);
        // printf("%s", data);
        request_callback->success(request, data);
    } while (r > 0);
    // printf("\n------\n... Finished Reading!\n\n");

    close(s);
    free(data);
    data = NULL;
}

void http_test_get() {
    http_request_t request;
    make_request(&request, "http://192.168.4.2/", HTTP_GET);
    http_requset_add_header(&request, "Accept-Language", "us-en");
    char *verbose = (char *) malloc(1024);
    make_request_text(&request, verbose, 1024);
    printf("Request:\n%s-----\n", verbose);
    free(verbose);
    verbose = NULL;
    http_request_send(&request);
}

void http_test_get_remote(void) {
    http_request_t request;
    make_request(&request, "http://davidkopala.com/lederbord/test.html", HTTP_GET);
    http_request_send(&request);
}

void http_test_post(void) {
    http_request_t request;
    make_request(&request, "http://192.168.4.2/", HTTP_POST);
    request.body = "Hello From ESP!";

    char *verbose = (char *) malloc(1024);
    bzero(verbose, 1024);
    make_request_text(&request, verbose, 1024);
    printf("Request:\n%s-----\n", verbose);
    free(verbose);
    verbose = NULL;

    http_request_send(&request);
}