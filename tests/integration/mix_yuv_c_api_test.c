#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "video/mix_yuv_c.h"

#ifndef YUVMIX_TEST_FONT
#define YUVMIX_TEST_FONT "/System/Library/Fonts/SFNSMono.ttf"
#endif

static int failures = 0;

#define EXPECT_TRUE(condition)                                                \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: expected %s\n", __FILE__, __LINE__,      \
                    #condition);                                              \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

#define EXPECT_STATUS(expected, expression)                                  \
    do {                                                                      \
        const yuvmix_status actual_status = (expression);                     \
        if (actual_status != (expected)) {                                    \
            fprintf(stderr, "%s:%d: expected status %d, got %d\n", __FILE__, \
                    __LINE__, (int)(expected), (int)actual_status);            \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

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
    yuvmix_config config = {
        .font_path = YUVMIX_TEST_FONT,
        .font_face_index = 0,
        .font_size = 32,
        .osd_left = 16,
        .osd_bottom = 16,
    };
    yuvmix_source source = {0};
    yuvmix_output output = {0};
    yuvmix_context* context = (yuvmix_context*)(uintptr_t)1;

    (void)source;
    (void)output;

    EXPECT_TRUE(YUVMIX_STATUS_OK == 0);
    EXPECT_TRUE(YUVMIX_STATUS_INTERNAL_ERROR == 6);
    EXPECT_TRUE(YUVMIX_FILL_MODE_CONTAIN == 0);
    EXPECT_TRUE(YUVMIX_FILL_MODE_COVER == 1);

    EXPECT_STATUS(YUVMIX_STATUS_INVALID_ARGUMENT,
                  yuvmix_context_create(NULL, &context));
    EXPECT_TRUE(context == NULL);

    EXPECT_STATUS(YUVMIX_STATUS_INVALID_ARGUMENT,
                  yuvmix_context_create(&config, NULL));

    config.font_path = NULL;
    context = (yuvmix_context*)(uintptr_t)1;
    EXPECT_STATUS(YUVMIX_STATUS_INVALID_ARGUMENT,
                  yuvmix_context_create(&config, &context));
    EXPECT_TRUE(context == NULL);

    config.font_path = YUVMIX_TEST_FONT;
    EXPECT_STATUS(YUVMIX_STATUS_OK, yuvmix_context_create(&config, &context));
    EXPECT_TRUE(context != NULL);

    yuvmix_context_destroy(context);
    yuvmix_context_destroy(NULL);

    return failures == 0 ? 0 : 1;
}
