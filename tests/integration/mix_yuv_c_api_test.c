#include <stddef.h>

#include "video/mix_yuv_c.h"

typedef yuvmix_status (*yuvmix_context_create_fn)(const yuvmix_config*,
                                                   yuvmix_context**);
typedef void (*yuvmix_context_destroy_fn)(yuvmix_context*);
typedef yuvmix_status (*yuvmix_mix_fn)(yuvmix_context*,
                                       const yuvmix_source*,
                                       size_t,
                                       yuvmix_output*);

_Static_assert(_Generic(&yuvmix_context_create,
                        yuvmix_context_create_fn: 1,
                        default: 0),
               "yuvmix_context_create signature changed");
_Static_assert(_Generic(&yuvmix_context_destroy,
                        yuvmix_context_destroy_fn: 1,
                        default: 0),
               "yuvmix_context_destroy signature changed");
_Static_assert(_Generic(&yuvmix_mix, yuvmix_mix_fn: 1, default: 0),
               "yuvmix_mix signature changed");

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
