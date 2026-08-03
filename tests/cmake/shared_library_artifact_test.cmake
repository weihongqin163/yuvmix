if(NOT DEFINED LIBRARY_PATH OR LIBRARY_PATH STREQUAL "")
    message(FATAL_ERROR "LIBRARY_PATH is required")
endif()

if(NOT DEFINED EXPECTED_SUFFIX OR EXPECTED_SUFFIX STREQUAL "")
    message(FATAL_ERROR "EXPECTED_SUFFIX is required")
endif()

if(NOT EXISTS "${LIBRARY_PATH}")
    message(FATAL_ERROR
        "Expected library artifact '${LIBRARY_PATH}' to exist")
endif()

string(LENGTH "${LIBRARY_PATH}" library_path_length)
string(LENGTH "${EXPECTED_SUFFIX}" expected_suffix_length)

if(library_path_length LESS expected_suffix_length)
    message(FATAL_ERROR
        "Expected library artifact '${LIBRARY_PATH}' to end with "
        "'${EXPECTED_SUFFIX}'")
endif()

math(EXPR suffix_offset "${library_path_length} - ${expected_suffix_length}")
string(SUBSTRING "${LIBRARY_PATH}" ${suffix_offset} -1 actual_suffix)

if(NOT actual_suffix STREQUAL EXPECTED_SUFFIX)
    message(FATAL_ERROR
        "Expected library artifact '${LIBRARY_PATH}' to end with "
        "'${EXPECTED_SUFFIX}', but its suffix is '${actual_suffix}'")
endif()
