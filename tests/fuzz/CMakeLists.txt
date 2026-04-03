# SPDX-License-Identifier: MIT
# eBootloader Fuzz Testing Targets
#
# Build with:
#   cmake -B build -DEBLDR_BUILD_FUZZ=ON -DCMAKE_C_COMPILER=clang
# Run:
#   ./build/tests/fuzz/fuzz_image_verify -max_total_time=60

option(EBLDR_BUILD_FUZZ "Build libFuzzer fuzz targets for eBootloader" OFF)

if(EBLDR_BUILD_FUZZ)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(WARNING "Fuzzing requires Clang — skipping fuzz targets")
        return()
    endif()

    set(FUZZ_FLAGS "-fsanitize=fuzzer,address -fno-omit-frame-pointer")

    # --- fuzz_image_verify ---
    add_executable(fuzz_image_verify fuzz_image_verify.c)
    target_link_libraries(fuzz_image_verify PRIVATE eboot_core)
    set_target_properties(fuzz_image_verify PROPERTIES
        COMPILE_FLAGS "${FUZZ_FLAGS}"
        LINK_FLAGS "${FUZZ_FLAGS}"
    )

    # --- fuzz_recovery_protocol ---
    add_executable(fuzz_recovery_protocol fuzz_recovery_protocol.c)
    target_link_libraries(fuzz_recovery_protocol PRIVATE eboot_core)
    set_target_properties(fuzz_recovery_protocol PROPERTIES
        COMPILE_FLAGS "${FUZZ_FLAGS}"
        LINK_FLAGS "${FUZZ_FLAGS}"
    )

    # --- fuzz_fw_update ---
    add_executable(fuzz_fw_update fuzz_fw_update.c)
    target_link_libraries(fuzz_fw_update PRIVATE eboot_core)
    set_target_properties(fuzz_fw_update PROPERTIES
        COMPILE_FLAGS "${FUZZ_FLAGS}"
        LINK_FLAGS "${FUZZ_FLAGS}"
    )

    # --- fuzz_crypto ---
    add_executable(fuzz_crypto fuzz_crypto.c)
    target_link_libraries(fuzz_crypto PRIVATE eboot_core)
    set_target_properties(fuzz_crypto PROPERTIES
        COMPILE_FLAGS "${FUZZ_FLAGS}"
        LINK_FLAGS "${FUZZ_FLAGS}"
    )

    # --- fuzz_bootctl ---
    add_executable(fuzz_bootctl fuzz_bootctl.c)
    target_link_libraries(fuzz_bootctl PRIVATE eboot_core)
    set_target_properties(fuzz_bootctl PROPERTIES
        COMPILE_FLAGS "${FUZZ_FLAGS}"
        LINK_FLAGS "${FUZZ_FLAGS}"
    )

    message(STATUS "  Fuzz targets: fuzz_image_verify, fuzz_recovery_protocol, fuzz_fw_update, fuzz_crypto, fuzz_bootctl")
endif()
