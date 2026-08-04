#include <stddef.h>

#include "video/mix_yuv_c.h"

int main(void) {
    yuvmix_config config = {0};
    yuvmix_source source = {0};
    yuvmix_output output = {0};
    yuvmix_context* context = NULL;

    (void)config;
    (void)source;
    (void)output;
    (void)context;

    return YUVMIX_STATUS_OK == 0 &&
                   YUVMIX_STATUS_INTERNAL_ERROR == 6 &&
                   YUVMIX_FILL_MODE_CONTAIN == 0 &&
                   YUVMIX_FILL_MODE_COVER == 1
               ? 0
               : 1;
}
