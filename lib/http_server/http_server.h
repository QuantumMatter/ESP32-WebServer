void http_server_task(void *pvParameters);
char *scan_url_encoded(char *data, char *key);
void http_server_start(void);

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

void http_server_set_client_callback(void (*callback)(struct http_request *request, char *buffer));
