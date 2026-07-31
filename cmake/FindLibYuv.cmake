find_path(LibYuv_INCLUDE_DIR
    NAMES libyuv/scale.h
    HINTS ${LibYuv_ROOT}
    PATH_SUFFIXES include)

if(LibYuv_INCLUDE_DIR AND
   NOT EXISTS "${LibYuv_INCLUDE_DIR}/libyuv/planar_functions.h")
    set(LibYuv_INCLUDE_DIR "LibYuv_INCLUDE_DIR-NOTFOUND")
endif()

find_library(LibYuv_LIBRARY
    NAMES yuv libyuv
    HINTS ${LibYuv_ROOT}
    PATH_SUFFIXES lib lib64)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibYuv
    REQUIRED_VARS LibYuv_INCLUDE_DIR LibYuv_LIBRARY)

if(LibYuv_FOUND AND NOT TARGET LibYuv::LibYuv)
    add_library(LibYuv::LibYuv UNKNOWN IMPORTED)
    set_target_properties(LibYuv::LibYuv PROPERTIES
        IMPORTED_LOCATION "${LibYuv_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LibYuv_INCLUDE_DIR}")
endif()

mark_as_advanced(LibYuv_INCLUDE_DIR LibYuv_LIBRARY)
