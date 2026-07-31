function(yuvmix_platform_dependency_name output_variable)
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(target_processor "${CMAKE_SYSTEM_PROCESSOR}")
        if(CMAKE_OSX_ARCHITECTURES)
            list(LENGTH CMAKE_OSX_ARCHITECTURES architecture_count)
            if(NOT architecture_count EQUAL 1)
                message(FATAL_ERROR
                    "Static dependency packages require one macOS architecture per build")
            endif()
            list(GET CMAKE_OSX_ARCHITECTURES 0 target_processor)
        endif()
        string(TOLOWER "${target_processor}" processor)
        if(processor STREQUAL "x86_64" OR processor STREQUAL "amd64")
            set(platform_name "macos-x86_64")
        elseif(processor STREQUAL "arm64" OR processor STREQUAL "aarch64")
            set(platform_name "macos-arm64")
        else()
            message(FATAL_ERROR
                "Unsupported macOS architecture: ${target_processor}")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" processor)
        if(processor STREQUAL "x86_64" OR processor STREQUAL "amd64")
            set(platform_name "linux-x86_64")
        else()
            message(FATAL_ERROR
                "Unsupported Linux architecture: ${CMAKE_SYSTEM_PROCESSOR}")
        endif()
    else()
        message(FATAL_ERROR "Unsupported operating system: ${CMAKE_SYSTEM_NAME}")
    endif()

    set(${output_variable} "${platform_name}" PARENT_SCOPE)
endfunction()

function(yuvmix_configure_static_dependencies)
    yuvmix_platform_dependency_name(platform_name)

    if(YUVMIX_DEPS_ROOT)
        set(deps_root "${YUVMIX_DEPS_ROOT}")
    else()
        set(deps_root
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/prebuilt/${platform_name}")
    endif()

    set(libyuv_include "${deps_root}/include")
    set(libyuv_archive "${deps_root}/lib/libyuv.a")
    set(freetype_include "${deps_root}/include/freetype2")
    set(freetype_archive "${deps_root}/lib/libfreetype.a")

    foreach(required_path IN ITEMS
            "${libyuv_include}/libyuv/scale.h"
            "${libyuv_include}/libyuv/planar_functions.h"
            "${libyuv_archive}"
            "${freetype_include}/ft2build.h"
            "${freetype_include}/freetype/config/ftheader.h"
            "${freetype_include}/freetype/freetype.h"
            "${freetype_archive}")
        if(NOT EXISTS "${required_path}")
            message(FATAL_ERROR
                "Static dependency package is incomplete: ${required_path}")
        endif()
    endforeach()

    add_library(LibYuv::LibYuv STATIC IMPORTED GLOBAL)
    set_target_properties(LibYuv::LibYuv PROPERTIES
        IMPORTED_LOCATION "${libyuv_archive}"
        INTERFACE_INCLUDE_DIRECTORIES "${libyuv_include}")

    add_library(YuvMix::Freetype STATIC IMPORTED GLOBAL)
    set_target_properties(YuvMix::Freetype PROPERTIES
        IMPORTED_LOCATION "${freetype_archive}"
        INTERFACE_INCLUDE_DIRECTORIES "${freetype_include}"
        INTERFACE_LINK_LIBRARIES "${YUVMIX_FREETYPE_STATIC_LINK_LIBRARIES}")

    message(STATUS "Using static dependencies from ${deps_root}")
endfunction()

function(yuvmix_configure_system_dependencies)
    find_package(Freetype REQUIRED)
    find_package(LibYuv REQUIRED)

    if(NOT TARGET YuvMix::Freetype)
        add_library(YuvMix::Freetype INTERFACE IMPORTED GLOBAL)
        set_target_properties(YuvMix::Freetype PROPERTIES
            INTERFACE_LINK_LIBRARIES Freetype::Freetype)
    endif()

    message(STATUS "Using system/development dependencies")
endfunction()

function(yuvmix_configure_dependencies)
    if(YUVMIX_LINK_DEPS_STATIC)
        yuvmix_configure_static_dependencies()
    else()
        yuvmix_configure_system_dependencies()
    endif()
endfunction()
