#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "video/mix_yuv_c.h"

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ != 201112L
#error "mix_yuv_c_api_test must be compiled as C11"
#endif

#ifndef __STRICT_ANSI__
#error "mix_yuv_c_api_test must be compiled in strict mode"
#endif

#ifndef YUVMIX_TEST_FONT
#error "YUVMIX_TEST_FONT must name a deterministic test font"
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

typedef struct owned_i420 {
    uint8_t* y;
    uint8_t* u;
    uint8_t* v;
    uint32_t width;
    uint32_t height;
    size_t y_size;
    size_t u_size;
    size_t v_size;
} owned_i420;

static void owned_i420_destroy(owned_i420* image) {
    if (image == NULL) {
        return;
    }
    free(image->y);
    free(image->u);
    free(image->v);
    memset(image, 0, sizeof(*image));
}

static int owned_i420_create(uint32_t width,
                             uint32_t height,
                             owned_i420* image) {
    const size_t chroma_width = width / 2;
    const size_t chroma_height = height / 2;

    if (image == NULL) {
        return 0;
    }
    memset(image, 0, sizeof(*image));
    image->width = width;
    image->height = height;
    image->y_size = (size_t)width * height;
    image->u_size = chroma_width * chroma_height;
    image->v_size = chroma_width * chroma_height;
    image->y = (uint8_t*)malloc(image->y_size);
    image->u = (uint8_t*)malloc(image->u_size);
    image->v = (uint8_t*)malloc(image->v_size);
    if (image->y == NULL || image->u == NULL || image->v == NULL) {
        owned_i420_destroy(image);
        return 0;
    }
    return 1;
}

static void owned_i420_fill(owned_i420* image,
                            uint8_t y,
                            uint8_t u,
                            uint8_t v) {
    memset(image->y, y, image->y_size);
    memset(image->u, u, image->u_size);
    memset(image->v, v, image->v_size);
}

static yuvmix_i420_image owned_i420_const_view(const owned_i420* image) {
    yuvmix_i420_image view;

    view.y.data = image->y;
    view.y.stride = (int)image->width;
    view.y.size = image->y_size;
    view.u.data = image->u;
    view.u.stride = (int)(image->width / 2);
    view.u.size = image->u_size;
    view.v.data = image->v;
    view.v.stride = (int)(image->width / 2);
    view.v.size = image->v_size;
    view.width = image->width;
    view.height = image->height;
    return view;
}

static yuvmix_mutable_i420_image owned_i420_mutable_view(owned_i420* image) {
    yuvmix_mutable_i420_image view;

    view.y.data = image->y;
    view.y.stride = (int)image->width;
    view.y.size = image->y_size;
    view.u.data = image->u;
    view.u.stride = (int)(image->width / 2);
    view.u.size = image->u_size;
    view.v.data = image->v;
    view.v.stride = (int)(image->width / 2);
    view.v.size = image->v_size;
    view.width = image->width;
    view.height = image->height;
    return view;
}

static void expect_color(const owned_i420* image,
                         uint32_t x,
                         uint32_t y,
                         uint8_t expected_y,
                         uint8_t expected_u,
                         uint8_t expected_v) {
    const size_t y_offset = (size_t)y * image->width + x;
    const size_t uv_offset =
        (size_t)(y / 2) * (image->width / 2) + x / 2;

    EXPECT_TRUE(image->y[y_offset] == expected_y);
    EXPECT_TRUE(image->u[uv_offset] == expected_u);
    EXPECT_TRUE(image->v[uv_offset] == expected_v);
}

int main(void) {
    yuvmix_config config = {
        .font_path = YUVMIX_TEST_FONT,
        .font_face_index = 0,
        .font_size = 32,
        .osd_left = 16,
        .osd_bottom = 16,
    };
    owned_i420 source_images[4] = {{0}};
    owned_i420 output_image = {0};
    yuvmix_source sources[4] = {{0}};
    yuvmix_output output = {0};
    yuvmix_context* context = (yuvmix_context*)(uintptr_t)1;
    size_t i;

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
    if (context == NULL) {
        goto cleanup;
    }

    for (i = 0; i < 4; ++i) {
        if (!owned_i420_create(640, 360, &source_images[i])) {
            fprintf(stderr, "failed to allocate source %zu\n", i);
            ++failures;
            goto cleanup;
        }
    }
    if (!owned_i420_create(1280, 720, &output_image)) {
        fprintf(stderr, "failed to allocate output\n");
        ++failures;
        goto cleanup;
    }

    owned_i420_fill(&source_images[0], 63, 102, 240);
    owned_i420_fill(&source_images[1], 32, 240, 118);
    owned_i420_fill(&source_images[2], 219, 16, 138);
    owned_i420_fill(&source_images[3], 173, 42, 26);
    for (i = 0; i < 4; ++i) {
        sources[i].image = owned_i420_const_view(&source_images[i]);
        sources[i].destination.x = (uint32_t)(i % 2) * 640;
        sources[i].destination.y = (uint32_t)(i / 2) * 360;
        sources[i].destination.w = 640;
        sources[i].destination.h = 360;
        sources[i].fill_mode =
            (i % 2) == 0 ? YUVMIX_FILL_MODE_CONTAIN : YUVMIX_FILL_MODE_COVER;
    }
    sources[0].display_name = "source-1";
    sources[1].display_name = "source-2";
    sources[2].display_name = "source-3";
    sources[3].display_name = NULL;
    sources[0].is_highlight = 1;

    output.image = owned_i420_mutable_view(&output_image);
    output.background_color.y = 16;
    output.background_color.u = 128;
    output.background_color.v = 128;

    EXPECT_STATUS(YUVMIX_STATUS_INVALID_ARGUMENT,
                  yuvmix_mix(NULL, sources, 4, &output));
    EXPECT_STATUS(YUVMIX_STATUS_INVALID_ARGUMENT,
                  yuvmix_mix(context, NULL, 1, &output));
    EXPECT_STATUS(YUVMIX_STATUS_INVALID_ARGUMENT,
                  yuvmix_mix(context, sources, 4, NULL));

    sources[1].fill_mode = (yuvmix_fill_mode)99;
    EXPECT_STATUS(YUVMIX_STATUS_INVALID_ARGUMENT,
                  yuvmix_mix(context, sources, 4, &output));
    sources[1].fill_mode = YUVMIX_FILL_MODE_COVER;

    EXPECT_STATUS(YUVMIX_STATUS_OK,
                  yuvmix_mix(context, sources, 4, &output));
    expect_color(&output_image, 320, 180, 63, 102, 240);
    expect_color(&output_image, 960, 180, 32, 240, 118);
    expect_color(&output_image, 320, 540, 219, 16, 138);
    expect_color(&output_image, 960, 540, 173, 42, 26);

cleanup:
    yuvmix_context_destroy(context);
    yuvmix_context_destroy(NULL);
    for (i = 0; i < 4; ++i) {
        owned_i420_destroy(&source_images[i]);
    }
    owned_i420_destroy(&output_image);

    return failures == 0 ? 0 : 1;
}
