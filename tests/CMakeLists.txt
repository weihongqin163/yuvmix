set(YUVMIX_TEST_FONT "/System/Library/Fonts/SFNSMono.ttf"
    CACHE FILEPATH "Deterministic font used by MixYuv OSD tests")

find_package(Threads REQUIRED)

if(NOT EXISTS "${YUVMIX_TEST_FONT}")
    message(FATAL_ERROR
        "YUVMIX_TEST_FONT must point to a readable TrueType/OpenType font")
endif()

function(add_yuvmix_test name)
    add_executable(${name} ${ARGN})
    target_compile_features(${name} PRIVATE cxx_std_11)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${name}
        PRIVATE YUVMIX_TEST_FONT="${YUVMIX_TEST_FONT}")
    target_link_libraries(${name} PRIVATE yuvmix_video)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_yuvmix_test(mix_yuv_api_test
    unit/mix_yuv_api_test.cc
    test_support/i420_test_image.cc)
add_yuvmix_test(i420_alpha_blend_test
    unit/i420_alpha_blend_test.cc
    test_support/i420_test_image.cc)
add_yuvmix_test(i420_alpha_blend_contract_test
    unit/i420_alpha_blend_contract_test.cc
    test_support/i420_test_image.cc)
target_link_libraries(i420_alpha_blend_contract_test PRIVATE Threads::Threads)
add_yuvmix_test(i420_geometry_test unit/i420_geometry_test.cc)
add_yuvmix_test(mix_yuv_test
    unit/mix_yuv_test.cc
    test_support/i420_test_image.cc)
add_yuvmix_test(i420_highlight_test
    unit/i420_highlight_test.cc
    test_support/i420_test_image.cc)
add_yuvmix_test(freetype_osd_test
    unit/freetype_osd_test.cc
    test_support/i420_test_image.cc)
add_yuvmix_test(mix_yuv_contract_test
    unit/mix_yuv_contract_test.cc
    test_support/i420_test_image.cc)
target_link_libraries(mix_yuv_contract_test PRIVATE Threads::Threads)

add_test(NAME dependency_mapping_test
    COMMAND "${CMAKE_COMMAND}"
        -DYUVMIX_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependency_mapping_test.cmake")

add_test(NAME shared_library_artifact_test
    COMMAND "${CMAKE_COMMAND}"
        -DLIBRARY_PATH=$<TARGET_FILE:yuvmix_video>
        -DEXPECTED_SUFFIX=${CMAKE_SHARED_LIBRARY_SUFFIX}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/shared_library_artifact_test.cmake")

add_executable(mix_yuv_c_api_test
    integration/mix_yuv_c_api_test.c)
set_target_properties(mix_yuv_c_api_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
target_compile_definitions(mix_yuv_c_api_test
    PRIVATE YUVMIX_TEST_FONT="${YUVMIX_TEST_FONT}")
target_link_libraries(mix_yuv_c_api_test PRIVATE yuvmix_video)
add_test(NAME mix_yuv_c_api_test COMMAND mix_yuv_c_api_test)

add_executable(i420_alpha_blend_c_api_test
    integration/i420_alpha_blend_c_api_test.c)
set_target_properties(i420_alpha_blend_c_api_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
target_link_libraries(i420_alpha_blend_c_api_test PRIVATE yuvmix_video)
add_test(NAME i420_alpha_blend_c_api_test COMMAND i420_alpha_blend_c_api_test)

add_yuvmix_test(mix_yuv_four_source_test
    integration/mix_yuv_four_source_test.cc
    test_support/i420_test_image.cc)
set_tests_properties(mix_yuv_four_source_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(mix_yuv_benchmark
    performance/mix_yuv_benchmark.cc
    test_support/i420_test_image.cc)
target_compile_features(mix_yuv_benchmark PRIVATE cxx_std_11)
target_include_directories(mix_yuv_benchmark
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(mix_yuv_benchmark
    PRIVATE YUVMIX_TEST_FONT="${YUVMIX_TEST_FONT}")
target_link_libraries(mix_yuv_benchmark PRIVATE yuvmix_video)

add_executable(i420_highlight_benchmark
    performance/i420_highlight_benchmark.cc
    test_support/i420_test_image.cc)
target_compile_features(i420_highlight_benchmark PRIVATE cxx_std_11)
target_include_directories(i420_highlight_benchmark
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(i420_highlight_benchmark PRIVATE yuvmix_video)
