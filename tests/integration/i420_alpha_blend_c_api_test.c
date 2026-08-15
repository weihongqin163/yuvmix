#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "video/mix_yuv_c.h"

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ != 201112L
#error "i420_alpha_blend_c_api_test must be compiled as C11"
#endif

#ifndef __STRICT_ANSI__
#error "i420_alpha_blend_c_api_test must be compiled in strict mode"
#endif

static int failures = 0;

#define EXPECT_TRUE(condition)            \
    do {                                  \
        if (!(condition)) {               \
            ++failures;                   \
        }                                 \
    } while (0)

#define EXPECT_STATUS(expected, expression)        \
    do {                                            \
        const yuvmix_status actual = (expression); \
        if (actual != (expected)) {                 \
            ++failures;                             \
        }                                           \
    } while (0)

typedef yuvmix_status (*yuvmix_alpha_blend_i420_fn)(
    const yuvmix_i420_blend_source*,
    size_t,
    yuvmix_mutable_i420_image*);

_Static_assert(_Generic(&yuvmix_alpha_blend_i420,
                        yuvmix_alpha_blend_i420_fn: 1,
                        default: 0),
               "yuvmix_alpha_blend_i420 signature changed");

int main(void) {
    uint8_t source_y[4];
    uint8_t source_u[1];
    uint8_t source_v[1];
    uint8_t background_y[16];
    uint8_t background_u[4];
    uint8_t background_v[4];
    yuvmix_i420_blend_source source = {0};
    yuvmix_mutable_i420_image background = {0};

    memset(source_y, 63, sizeof(source_y));
    memset(source_u, 102, sizeof(source_u));
    memset(source_v, 240, sizeof(source_v));
    memset(background_y, 16, sizeof(background_y));
    memset(background_u, 128, sizeof(background_u));
    memset(background_v, 128, sizeof(background_v));

    source.image.y = (yuvmix_const_plane){source_y, 2, sizeof(source_y)};
    source.image.u = (yuvmix_const_plane){source_u, 1, sizeof(source_u)};
    source.image.v = (yuvmix_const_plane){source_v, 1, sizeof(source_v)};
    source.image.width = 2;
    source.image.height = 2;
    source.x = 0;
    source.y = 0;
    source.alpha = 128;

    background.y =
        (yuvmix_mutable_plane){background_y, 4, sizeof(background_y)};
    background.u =
        (yuvmix_mutable_plane){background_u, 2, sizeof(background_u)};
    background.v =
        (yuvmix_mutable_plane){background_v, 2, sizeof(background_v)};
    background.width = 4;
    background.height = 4;

    EXPECT_STATUS(YUVMIX_STATUS_INVALID_ARGUMENT,
                  yuvmix_alpha_blend_i420(NULL, 1, &background));
    EXPECT_STATUS(YUVMIX_STATUS_INVALID_ARGUMENT,
                  yuvmix_alpha_blend_i420(&source, 1, NULL));
    EXPECT_STATUS(YUVMIX_STATUS_OK,
                  yuvmix_alpha_blend_i420(NULL, 0, &background));
    EXPECT_STATUS(YUVMIX_STATUS_OK,
                  yuvmix_alpha_blend_i420(&source, 1, &background));

    EXPECT_TRUE(background_y[0] == 40);
    EXPECT_TRUE(background_y[5] == 40);
    EXPECT_TRUE(background_y[15] == 16);
    EXPECT_TRUE(background_u[0] == 115);
    EXPECT_TRUE(background_u[3] == 128);
    EXPECT_TRUE(background_v[0] == 184);
    EXPECT_TRUE(background_v[3] == 128);
    return failures == 0 ? 0 : 1;
}
