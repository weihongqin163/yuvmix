cmake_minimum_required(VERSION 3.20)

project(yuvmix LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(YUVMIX_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(YUVMIX_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(YUVMIX_LINK_DEPS_STATIC
       "Use platform-specific prebuilt static dependencies" ON)
set(YUVMIX_DEPS_ROOT "" CACHE PATH "Platform dependency package root")
set(YUVMIX_FREETYPE_STATIC_LINK_LIBRARIES "" CACHE STRING
    "Additional libraries required by the prebuilt static FreeType")

if(YUVMIX_ENABLE_ASAN OR YUVMIX_ENABLE_UBSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR
            "Sanitizers require a Clang or GCC compatible compiler")
    endif()
endif()

if(YUVMIX_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(YUVMIX_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()

include(YuvMixDependencies)
yuvmix_configure_dependencies()

add_library(yuvmix_video SHARED
    src/video/i420_geometry.cc
    src/video/i420_highlight.cc
    src/video/mix_yuv_c.cc
    src/video/mix_yuv.cc
    src/video/freetype_osd.cc)
target_compile_features(yuvmix_video PUBLIC cxx_std_11)
set_target_properties(yuvmix_video PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET default)
target_include_directories(yuvmix_video
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")
target_link_libraries(yuvmix_video
    PRIVATE YuvMix::Freetype
    PUBLIC LibYuv::LibYuv)
target_link_options(yuvmix_video PRIVATE
    "$<$<PLATFORM_ID:Linux>:LINKER:-z,defs>")

include(CTest)
if(BUILD_TESTING)
    target_compile_definitions(yuvmix_video PUBLIC YUVMIX_TESTING)
    add_subdirectory(tests)
endif()
