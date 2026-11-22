# Pure CMake script to embed markdown guide files into GuideEmbed.cpp as byte arrays.
# Uses unsigned char byte arrays rather than string literals to avoid MSVC C2026 length limits.

if(NOT DEFINED PROMPT_FILE OR NOT DEFINED REFERENCE_FILE OR NOT DEFINED TEMPLATE_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "EmbedGuideDocs.cmake requires PROMPT_FILE, REFERENCE_FILE, TEMPLATE_FILE, and OUTPUT_FILE")
endif()

function(file_to_c_byte_array input_path out_var)
    if(NOT EXISTS "${input_path}")
        message(FATAL_ERROR "Guide file not found: ${input_path}")
    endif()

    file(READ "${input_path}" hex_content HEX)
    if(hex_content STREQUAL "")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    # Format every byte as 0xXX, then normalize CRLF (0x0d,0x0a,) -> LF (0x0a,) on byte boundaries.
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," c_bytes "${hex_content}")
    string(REPLACE "0x0d,0x0a," "0x0a," c_bytes "${c_bytes}")

    # Wrap every 16 bytes for compiler friendliness and readability (CMake regex lacks {N}).
    set(_b "0x[0-9a-f][0-9a-f],")
    string(REGEX REPLACE
        "(${_b}${_b}${_b}${_b}${_b}${_b}${_b}${_b}${_b}${_b}${_b}${_b}${_b}${_b}${_b}${_b})"
        "\\1\n    "
        c_bytes
        "${c_bytes}"
    )

    set(${out_var} "${c_bytes}" PARENT_SCOPE)
endfunction()

file_to_c_byte_array("${PROMPT_FILE}" GUIDE_PROMPT_BYTES)
file_to_c_byte_array("${REFERENCE_FILE}" GUIDE_REFERENCE_BYTES)

get_filename_component(output_dir "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")

configure_file("${TEMPLATE_FILE}" "${OUTPUT_FILE}" @ONLY)
