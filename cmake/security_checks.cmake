# SPDX-License-Identifier: MIT
# cmake/security_checks.cmake
# Static-analysis targets for eBootloader security hardening
#
# Usage:
#   cmake -B build -DEBLDR_ENABLE_SECURITY_CHECKS=ON
#   cmake --build build --target security-check-cppcheck
#   cmake --build build --target security-check-clang-tidy

option(EBLDR_ENABLE_SECURITY_CHECKS
       "Enable cppcheck and clang-tidy security analysis targets" OFF)

if(EBLDR_ENABLE_SECURITY_CHECKS)

    # ---- cppcheck ----
    find_program(CPPCHECK_EXECUTABLE cppcheck)

    if(CPPCHECK_EXECUTABLE)
        message(STATUS "cppcheck found: ${CPPCHECK_EXECUTABLE}")

        set(CPPCHECK_ARGS
            --enable=all
            --suppress=missingIncludeSystem
            --suppress=unusedFunction
            --error-exitcode=1
            --inline-suppr
            --std=c11
            -I ${CMAKE_SOURCE_DIR}/include
            -I ${CMAKE_SOURCE_DIR}/stage0
            -I ${CMAKE_SOURCE_DIR}/stage1
            -I ${CMAKE_SOURCE_DIR}/stage2
            -I ${CMAKE_SOURCE_DIR}/crypto
            -I ${CMAKE_SOURCE_DIR}/keystore
            ${CMAKE_SOURCE_DIR}/stage0/
            ${CMAKE_SOURCE_DIR}/stage1/
            ${CMAKE_SOURCE_DIR}/stage2/
            ${CMAKE_SOURCE_DIR}/crypto/
            ${CMAKE_SOURCE_DIR}/keystore/
            ${CMAKE_SOURCE_DIR}/recovery/
        )

        add_custom_target(security-check-cppcheck
            COMMAND ${CPPCHECK_EXECUTABLE} ${CPPCHECK_ARGS}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running cppcheck security analysis on eBootloader..."
        )
    else()
        message(WARNING "cppcheck not found — 'security-check-cppcheck' target unavailable")
    endif()

    # ---- clang-tidy ----
    find_program(CLANG_TIDY_EXECUTABLE clang-tidy)

    if(CLANG_TIDY_EXECUTABLE)
        message(STATUS "clang-tidy found: ${CLANG_TIDY_EXECUTABLE}")

        file(GLOB_RECURSE EBLDR_SOURCES
            ${CMAKE_SOURCE_DIR}/stage0/*.c
            ${CMAKE_SOURCE_DIR}/stage1/*.c
            ${CMAKE_SOURCE_DIR}/stage2/*.c
            ${CMAKE_SOURCE_DIR}/crypto/*.c
            ${CMAKE_SOURCE_DIR}/keystore/*.c
            ${CMAKE_SOURCE_DIR}/recovery/*.c
        )

        add_custom_target(security-check-clang-tidy
            COMMAND ${CLANG_TIDY_EXECUTABLE}
                    -p ${CMAKE_BINARY_DIR}
                    --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
                    ${EBLDR_SOURCES}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running clang-tidy security analysis on eBootloader..."
        )
    else()
        message(WARNING "clang-tidy not found — 'security-check-clang-tidy' target unavailable")
    endif()

endif()
