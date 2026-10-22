function(matching_enable_warnings target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "Cannot enable warnings for missing target: ${target_name}")
    endif()

    if(MSVC)
        target_compile_options("${target_name}" PRIVATE /W4 /permissive-)
        if(MATCHING_WARNINGS_AS_ERRORS)
            target_compile_options("${target_name}" PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang|GNU)$")
        target_compile_options(
            "${target_name}"
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wshadow
                -Wconversion
                -Wsign-conversion
        )
        if(MATCHING_WARNINGS_AS_ERRORS)
            target_compile_options("${target_name}" PRIVATE -Werror)
        endif()
    else()
        message(WARNING "No warning policy is defined for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()
