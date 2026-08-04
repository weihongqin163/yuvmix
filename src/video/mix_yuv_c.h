#ifndef YUVMIX_VIDEO_MIX_YUV_C_H_
#define YUVMIX_VIDEO_MIX_YUV_C_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum yuvmix_fill_mode {
    YUVMIX_FILL_MODE_CONTAIN = 0,
    YUVMIX_FILL_MODE_COVER = 1
} yuvmix_fill_mode;

typedef enum yuvmix_status {
    YUVMIX_STATUS_OK = 0,
    YUVMIX_STATUS_INVALID_ARGUMENT = 1,
    YUVMIX_STATUS_BUFFER_TOO_SMALL = 2,
    YUVMIX_STATUS_OUT_OF_MEMORY = 3,
    YUVMIX_STATUS_FONT_ERROR = 4,
    YUVMIX_STATUS_LIBYUV_ERROR = 5,
    YUVMIX_STATUS_INTERNAL_ERROR = 6
} yuvmix_status;

typedef struct yuvmix_const_plane {
    const uint8_t* data;
    int stride;
    size_t size;
} yuvmix_const_plane;

typedef struct yuvmix_mutable_plane {
    uint8_t* data;
    int stride;
    size_t size;
} yuvmix_mutable_plane;

typedef struct yuvmix_i420_image {
    yuvmix_const_plane y;
    yuvmix_const_plane u;
    yuvmix_const_plane v;
    uint32_t width;
    uint32_t height;
} yuvmix_i420_image;

typedef struct yuvmix_mutable_i420_image {
    yuvmix_mutable_plane y;
    yuvmix_mutable_plane u;
    yuvmix_mutable_plane v;
    uint32_t width;
    uint32_t height;
} yuvmix_mutable_i420_image;

typedef struct yuvmix_rect {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} yuvmix_rect;

typedef struct yuvmix_source {
    yuvmix_i420_image image;
    yuvmix_rect destination;
    const char* display_name;
    yuvmix_fill_mode fill_mode;
    int is_highlight;
} yuvmix_source;

typedef struct yuvmix_i420_color {
    uint8_t y;
    uint8_t u;
    uint8_t v;
} yuvmix_i420_color;

typedef struct yuvmix_output {
    yuvmix_mutable_i420_image image;
    yuvmix_i420_color background_color;
} yuvmix_output;

typedef struct yuvmix_config {
    const char* font_path;
    uint32_t font_face_index;
    uint32_t font_size;
    uint32_t osd_left;
    uint32_t osd_bottom;
} yuvmix_config;

typedef struct yuvmix_context yuvmix_context;

yuvmix_status yuvmix_context_create(const yuvmix_config* config,
                                    yuvmix_context** context);
void yuvmix_context_destroy(yuvmix_context* context);
yuvmix_status yuvmix_mix(yuvmix_context* context,
                         const yuvmix_source* sources,
                         size_t source_count,
                         yuvmix_output* output);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* YUVMIX_VIDEO_MIX_YUV_C_H_ */
