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

expect_platform("Darwin" "x86_64" "" "macos-x86_64")
expect_platform("Darwin" "arm64" "" "macos-arm64")
expect_platform("Darwin" "arm64" "x86_64" "macos-x86_64")
expect_platform("Darwin" "x86_64" "arm64" "macos-arm64")
expect_platform("Linux" "x86_64" "" "linux-x86_64")
