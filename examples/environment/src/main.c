#include <libdragon.h>
#include "console.h"

extern const char** environ;

int main(void) {
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_DISABLED);
    console_init();

    int i = 0;
    while (1) {
        const char* str = environ[i++];
        if (str == NULL) break;
        printf("%s\n", str);
    }

    while (1);
}
