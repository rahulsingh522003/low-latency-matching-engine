function(matching_enable_sanitizers target_name)
    if(NOT MATCHING_ENABLE_SANITIZERS)
        return()
    endif()

    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "Cannot enable sanitizers for missing target: ${target_name}")
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang|GNU)$")
        target_compile_options(
            "${target_name}"
            PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
                -fno-sanitize-recover=all
        )
        target_link_options("${target_name}" PRIVATE -fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "The sanitizer configuration requires Clang or GCC")
    endif()
endfunction()
