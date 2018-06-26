#ifndef http_constants_def
#define http_constants_def

#include <freertos/FreeRTOS.h>
#include <string.h>

typedef enum {
    HTTP_GET = 1,
    HTTP_POST = 2,
    HTTP_ERROR = -1
} http_method_t;

bool str_cmp(char *a, char *b);

#endif  //http_constants_def