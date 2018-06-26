#include <http_constants.h>
#include <string.h>

#ifndef http_request_def
#define http_request_def

typedef struct http_request {
    http_method_t method;
    char *host;
    char *path;
    char *body;
    char *headers;
} http_request_t;

typedef enum {
    HTTP_REQUEST_FAIL_DNS = 1,
    HTTP_REQUEST_FAIL_SOCKET,
    HTTP_REQUEST_FAIL_CONNECT,
    HTTP_REQUEST_FAIL_SEND,
    HTTP_REQUEST_FAIL_SET_TIMEOUT,
} http_request_fail_t;

void make_request(http_request_t *request, char *url, http_method_t method);
void make_request_from_text(http_method_t *request, char *data);
void make_request_text(http_request_t *request, char *buffer, size_t sz);
void http_requset_add_header(http_request_t *request, char *key, char *value);

void http_send_request(http_request_t *request);
void http_test_get();
void http_test_get_remote(void);
void http_test_post(void);

void http_request_default_fail(http_request_t *request, http_request_fail_t error_code);
void http_request_default_success(http_request_t *request, char *data);
typedef struct {
    void (*fail)(http_request_t *request, http_request_fail_t error_code);
    void (*success)(http_request_t *request, char *data);
} http_request_callback_t;
void http_request_set_callback(http_request_callback_t *callback);

#endif  //http_request_def