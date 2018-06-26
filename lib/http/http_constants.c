#include <http_constants.h>

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