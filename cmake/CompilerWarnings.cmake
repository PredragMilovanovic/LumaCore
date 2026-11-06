# Strict warnings for LumaCore's own targets only (never applied to third-party code).
function(lumacore_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4            # high warning level
            /WX            # warnings are errors
            /permissive-   # standards conformance
            /utf-8         # source and execution charset
            /Zc:__cplusplus
            /w14242        # possible loss of data in conversion
            /w14254        # larger bit field assigned to smaller
            /w14263        # member function hides base virtual
            /w14265        # class with virtuals has non-virtual destructor
            /w14287        # unsigned/negative constant mismatch
            /w14296        # expression always true/false
            /w14311        # pointer truncation
            /w14826        # sign-extending conversion
            /w14905 /w14906
            /w14928)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wshadow -Wconversion -Wsign-conversion
            -Wnon-virtual-dtor -Wold-style-cast -Wdouble-promotion)
    endif()
endfunction()
