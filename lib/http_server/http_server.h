#include <http_constants.h>
#include <http_requset.h>

#ifndef http_server_def
#define http_server_def

void http_server_task(void *pvParameters);
char *scan_url_encoded(char *data, char *key);
void http_server_start(void);
void http_server_stop(void);
void http_server_restart(void);

void http_server_set_client_callback(void (*callback)(struct http_request *request, char *buffer));

#endif  //http_server_def