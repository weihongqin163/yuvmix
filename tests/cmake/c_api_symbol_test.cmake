if(NOT DEFINED NM_TOOL OR NOT DEFINED LIBRARY_PATH)
    message(FATAL_ERROR "NM_TOOL and LIBRARY_PATH are required")
endif()

execute_process(
    COMMAND "${NM_TOOL}" -g "${LIBRARY_PATH}"
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE symbols
    ERROR_VARIABLE nm_error)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "nm failed: ${nm_error}")
endif()

string(FIND "${symbols}" "yuvmix_alpha_blend_i420" symbol_index)
if(symbol_index EQUAL -1)
    message(FATAL_ERROR
        "shared library does not export yuvmix_alpha_blend_i420")
endif()
