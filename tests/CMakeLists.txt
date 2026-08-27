# eBootloader Unit Tests
# All tests link against core libraries and run natively (no board ports needed).

# --- test_bootctl: Boot control block ---
add_executable(test_bootctl unit/test_bootctl.c)
target_link_libraries(test_bootctl PRIVATE eboot_core)
add_test(NAME test_bootctl COMMAND test_bootctl)

# --- test_crypto: SHA-256 against known vectors ---
add_executable(test_crypto unit/test_crypto.c)
target_link_libraries(test_crypto PRIVATE eboot_core)
add_test(NAME test_crypto COMMAND test_crypto)

# --- test_image_verify: Image header parse bounds ---
add_executable(test_image_verify unit/test_image_verify.c)
target_link_libraries(test_image_verify PRIVATE eboot_core)
add_test(NAME test_image_verify COMMAND test_image_verify)

# --- test_recovery: UART recovery write range ---
add_executable(test_recovery unit/test_recovery.c)
target_link_libraries(test_recovery PRIVATE eboot_core eboot_stage1)
add_test(NAME test_recovery COMMAND test_recovery)

# --- test_fw_transport: UART raw/YMODEM firmware transport ---
add_executable(test_fw_transport unit/test_fw_transport.c)
target_link_libraries(test_fw_transport PRIVATE eboot_core)
add_test(NAME test_fw_transport COMMAND test_fw_transport)

# --- test_device_table: UEFI-style device table ---
add_executable(test_device_table unit/test_device_table.c)
target_link_libraries(test_device_table PRIVATE eboot_core)
add_test(NAME test_device_table COMMAND test_device_table)

# --- test_runtime_svc: Runtime variable store ---
add_executable(test_runtime_svc unit/test_runtime_svc.c)
target_link_libraries(test_runtime_svc PRIVATE eboot_core)
add_test(NAME test_runtime_svc COMMAND test_runtime_svc)

# --- test_board_config: Declarative hardware config ---
add_executable(test_board_config unit/test_board_config.c)
target_link_libraries(test_board_config PRIVATE eboot_core)
add_test(NAME test_board_config COMMAND test_board_config)

# --- test_multicore: Multicore boot management ---
add_executable(test_multicore unit/test_multicore.c)
target_link_libraries(test_multicore PRIVATE eboot_core)
add_test(NAME test_multicore COMMAND test_multicore)

# --- test_board_registry: Runtime board selection ---
add_executable(test_board_registry unit/test_board_registry.c)
target_link_libraries(test_board_registry PRIVATE eboot_core)
add_test(NAME test_board_registry COMMAND test_board_registry)

# --- test_slot_manager: Firmware slot management ---
add_executable(test_slot_manager unit/test_slot_manager.c)
target_link_libraries(test_slot_manager PRIVATE eboot_core)
add_test(NAME test_slot_manager COMMAND test_slot_manager)

# --- test_boot_log: Boot log subsystem ---
add_executable(test_boot_log unit/test_boot_log.c)
target_link_libraries(test_boot_log PRIVATE eboot_core)
add_test(NAME test_boot_log COMMAND test_boot_log)

# --- test_ed25519: Ed25519 signature verification ---
add_executable(test_ed25519 unit/test_ed25519.c)
target_link_libraries(test_ed25519 PRIVATE eboot_core)
add_test(NAME test_ed25519 COMMAND test_ed25519)

# --- test_keystore: Key management ---
add_executable(test_keystore unit/test_keystore.c)
target_link_libraries(test_keystore PRIVATE eboot_core)
add_test(NAME test_keystore COMMAND test_keystore)

# --- test_rollback: Anti-rollback security counter ---
add_executable(test_rollback unit/test_rollback.c)
target_link_libraries(test_rollback PRIVATE eboot_core)
add_test(NAME test_rollback COMMAND test_rollback)

# --- test_storage: Unified storage abstraction ---
add_executable(test_storage unit/test_storage.c)
target_link_libraries(test_storage PRIVATE eboot_core)
add_test(NAME test_storage COMMAND test_storage)

# --- Valgrind test targets ---
find_program(VALGRIND valgrind)
if(VALGRIND)
    set(VALGRIND_OPTS --leak-check=full --error-exitcode=1 --quiet)
    foreach(TEST_NAME test_bootctl test_crypto test_ed25519 test_keystore
                      test_device_table test_runtime_svc test_board_config
                      test_multicore test_board_registry test_slot_manager
                      test_boot_log test_image_verify test_recovery
                      test_fw_transport)
        add_test(
            NAME valgrind_${TEST_NAME}
            COMMAND ${VALGRIND} ${VALGRIND_OPTS} $<TARGET_FILE:${TEST_NAME}>
        )
        set_tests_properties(valgrind_${TEST_NAME} PROPERTIES LABELS "valgrind")
    endforeach()
endif()

# --- Fuzz targets ---
if(EBLDR_BUILD_FUZZ)
    add_subdirectory(fuzz)
endif()
