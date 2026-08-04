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

function(expect_platform_rejected system processor osx_architectures expected_diagnostic)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DYUVMIX_SOURCE_DIR:STRING=${YUVMIX_SOURCE_DIR}"
            "-DPROBE_SYSTEM:STRING=${system}"
            "-DPROBE_PROCESSOR:STRING=${processor}"
            "-DPROBE_OSX_ARCHITECTURES:STRING=${osx_architectures}"
            -P "${CMAKE_CURRENT_LIST_DIR}/platform_mapping_probe.cmake"
        RESULT_VARIABLE probe_result
        OUTPUT_VARIABLE probe_output
        ERROR_VARIABLE probe_error)
    if(NOT probe_result MATCHES "^[0-9]+$")
        message(FATAL_ERROR
            "Platform probe did not return a numeric exit code\n"
            "Result: ${probe_result}\nOutput: ${probe_output}\nError: ${probe_error}")
    endif()
    if(probe_result EQUAL 0)
        message(FATAL_ERROR
            "Expected platform rejection for ${system}/${processor}/${osx_architectures}\n"
            "Result: ${probe_result}\nOutput: ${probe_output}\nError: ${probe_error}")
    endif()
    string(FIND "${probe_error}" "${expected_diagnostic}" diagnostic_position)
    if(diagnostic_position EQUAL -1)
        message(FATAL_ERROR
            "Platform rejection did not contain expected diagnostic: ${expected_diagnostic}\n"
            "Result: ${probe_result}\nOutput: ${probe_output}\nError: ${probe_error}")
    endif()
endfunction()

expect_platform("Darwin" "arm64" "" "macos-arm64")
expect_platform("Darwin" "aarch64" "" "macos-arm64")
expect_platform("Darwin" "x86_64" "arm64" "macos-arm64")
expect_platform("Linux" "x86_64" "" "linux-x86_64")
expect_platform("Linux" "amd64" "" "linux-x86_64")

expect_platform_rejected(
    "Darwin" "x86_64" "" "Unsupported macOS architecture: x86_64")
expect_platform_rejected(
    "Darwin" "arm64" "x86_64" "Unsupported macOS architecture: x86_64")
expect_platform_rejected(
    "Darwin" "arm64" "arm64;x86_64"
    "Static dependency packages require one macOS architecture per build")
