include("${YUVMIX_SOURCE_DIR}/cmake/YuvMixDependencies.cmake")

function(expect_platform system processor osx_architectures expected)
    set(CMAKE_SYSTEM_NAME "${system}")
    set(CMAKE_SYSTEM_PROCESSOR "${processor}")
    set(CMAKE_OSX_ARCHITECTURES "${osx_architectures}")
    yuvmix_platform_dependency_name(actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR
            "Expected ${expected} for ${system}/${processor}/${osx_architectures}, got ${actual}")
    endif()
endfunction()

function(expect_platform_rejected system processor osx_architectures)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DYUVMIX_SOURCE_DIR=${YUVMIX_SOURCE_DIR}
            -DPROBE_SYSTEM=${system}
            -DPROBE_PROCESSOR=${processor}
            -DPROBE_OSX_ARCHITECTURES=${osx_architectures}
            -P "${CMAKE_CURRENT_LIST_DIR}/platform_mapping_probe.cmake"
        RESULT_VARIABLE probe_result
        OUTPUT_QUIET
        ERROR_QUIET)
    if(probe_result EQUAL 0)
        message(FATAL_ERROR
            "Expected platform rejection for ${system}/${processor}/${osx_architectures}")
    endif()
endfunction()

expect_platform("Darwin" "arm64" "" "macos-arm64")
expect_platform("Darwin" "x86_64" "arm64" "macos-arm64")
expect_platform("Linux" "x86_64" "" "linux-x86_64")

expect_platform_rejected("Darwin" "x86_64" "")
expect_platform_rejected("Darwin" "arm64" "x86_64")
