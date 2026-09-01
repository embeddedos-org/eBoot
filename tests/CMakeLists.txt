# eBootloader Unit Tests
# All tests link against core libraries and run natively (no board ports needed).

# --- test_bootctl: Boot control block ---
add_executable(eboot_test_bootctl unit/test_bootctl.c)
target_link_libraries(eboot_test_bootctl PRIVATE eboot_core)
add_test(NAME test_bootctl COMMAND eboot_test_bootctl)

# --- test_crypto: SHA-256 against known vectors ---
add_executable(eboot_test_crypto unit/test_crypto.c)
target_link_libraries(eboot_test_crypto PRIVATE eboot_core)
add_test(NAME test_crypto COMMAND eboot_test_crypto)

# --- test_image_verify: Image header parse bounds ---
add_executable(eboot_test_image_verify unit/test_image_verify.c)
target_link_libraries(eboot_test_image_verify PRIVATE eboot_core)
add_test(NAME test_image_verify COMMAND eboot_test_image_verify)

# --- test_image_abi: pins the .efw image header wire format against eFirmware ---
add_executable(eboot_test_image_abi unit/test_image_abi.c)
target_link_libraries(eboot_test_image_abi PRIVATE eboot_core)
add_test(NAME test_image_abi COMMAND eboot_test_image_abi)

# --- test_secure_boot: Secure boot policy gates ---
add_executable(eboot_test_secure_boot unit/test_secure_boot.c)
target_link_libraries(eboot_test_secure_boot PRIVATE eboot_core)
add_test(NAME test_secure_boot COMMAND eboot_test_secure_boot)

# --- test_recovery: UART recovery write range ---
# eboot_stage1 links eboot_core PUBLIC, so naming eboot_core here too put it on
# the link line twice -- ahead of the target that needs it, which is the wrong
# order for a left-to-right archive scan, so CMake appended it again anyway and
# the linker reported the duplicate. The transitive dependency covers it.
add_executable(eboot_test_recovery unit/test_recovery.c)
target_link_libraries(eboot_test_recovery PRIVATE eboot_stage1)
add_test(NAME test_recovery COMMAND eboot_test_recovery)

# --- test_fw_transport: UART raw/XMODEM/YMODEM firmware transport ---
add_executable(eboot_test_fw_transport unit/test_fw_transport.c)
target_link_libraries(eboot_test_fw_transport PRIVATE eboot_core)
add_test(NAME test_fw_transport COMMAND eboot_test_fw_transport)

# --- test_slot_size_bounds: verify_slot() must reject image_size > slot capacity ---
add_executable(eboot_test_slot_size_bounds unit/test_slot_size_bounds.c)
target_link_libraries(eboot_test_slot_size_bounds PRIVATE eboot_core)
add_test(NAME test_slot_size_bounds COMMAND eboot_test_slot_size_bounds)

# --- test_device_table: UEFI-style device table ---
add_executable(eboot_test_device_table unit/test_device_table.c)
target_link_libraries(eboot_test_device_table PRIVATE eboot_core)
add_test(NAME test_device_table COMMAND eboot_test_device_table)

# --- test_runtime_svc: Runtime variable store ---
add_executable(eboot_test_runtime_svc unit/test_runtime_svc.c)
target_link_libraries(eboot_test_runtime_svc PRIVATE eboot_core)
add_test(NAME test_runtime_svc COMMAND eboot_test_runtime_svc)

# --- test_board_config: Declarative hardware config ---
add_executable(eboot_test_board_config unit/test_board_config.c)
target_link_libraries(eboot_test_board_config PRIVATE eboot_core)
add_test(NAME test_board_config COMMAND eboot_test_board_config)

# --- test_multicore: Multicore boot management ---
add_executable(eboot_test_multicore unit/test_multicore.c)
target_link_libraries(eboot_test_multicore PRIVATE eboot_core)
add_test(NAME test_multicore COMMAND eboot_test_multicore)

# --- test_board_registry: Runtime board selection ---
add_executable(eboot_test_board_registry unit/test_board_registry.c)
target_link_libraries(eboot_test_board_registry PRIVATE eboot_core)
add_test(NAME test_board_registry COMMAND eboot_test_board_registry)

# --- test_slot_manager: Firmware slot management ---
add_executable(eboot_test_slot_manager unit/test_slot_manager.c)
target_link_libraries(eboot_test_slot_manager PRIVATE eboot_core)
add_test(NAME test_slot_manager COMMAND eboot_test_slot_manager)

# --- test_boot_log: Boot log subsystem ---
add_executable(eboot_test_boot_log unit/test_boot_log.c)
target_link_libraries(eboot_test_boot_log PRIVATE eboot_core)
add_test(NAME test_boot_log COMMAND eboot_test_boot_log)

# --- test_ed25519: Ed25519 signature verification ---
add_executable(eboot_test_ed25519 unit/test_ed25519.c)
target_link_libraries(eboot_test_ed25519 PRIVATE eboot_core)
add_test(NAME test_ed25519 COMMAND eboot_test_ed25519)

# --- test_keystore: Key management ---
add_executable(eboot_test_keystore unit/test_keystore.c)
target_link_libraries(eboot_test_keystore PRIVATE eboot_core)
add_test(NAME test_keystore COMMAND eboot_test_keystore)

# --- test_rollback: Anti-rollback security counter ---
add_executable(eboot_test_rollback unit/test_rollback.c)
target_link_libraries(eboot_test_rollback PRIVATE eboot_core)
add_test(NAME test_rollback COMMAND eboot_test_rollback)

# --- test_storage: Unified storage abstraction ---
add_executable(eboot_test_storage unit/test_storage.c)
target_link_libraries(eboot_test_storage PRIVATE eboot_core)
add_test(NAME test_storage COMMAND eboot_test_storage)

# --- test_ecc: ECC memory range validation ---
add_executable(eboot_test_ecc unit/test_ecc.c)
target_link_libraries(eboot_test_ecc PRIVATE eboot_core)
add_test(NAME test_ecc COMMAND eboot_test_ecc)

# --- Valgrind test targets ---
find_program(VALGRIND valgrind)
if(VALGRIND)
    set(VALGRIND_OPTS --leak-check=full --error-exitcode=1 --quiet)
    foreach(TEST_NAME test_bootctl test_crypto test_ed25519 test_keystore
                      test_device_table test_runtime_svc test_board_config
                      test_multicore test_board_registry test_slot_manager
                      test_boot_log test_image_verify test_image_abi
                      test_recovery test_slot_size_bounds test_fw_transport)
        add_test(
            NAME valgrind_${TEST_NAME}
            COMMAND ${VALGRIND} ${VALGRIND_OPTS} $<TARGET_FILE:eboot_${TEST_NAME}>
        )
        set_tests_properties(valgrind_${TEST_NAME} PROPERTIES LABELS "valgrind")
    endforeach()
endif()

# --- Fuzz targets ---
if(EBLDR_BUILD_FUZZ)
    add_subdirectory(fuzz)
endif()

# --- Target namespace guard ---
#
# ebuild composes eos and eBoot into one CMake project (add_subdirectory), and
# CMake requires target names to be globally unique. test_crypto and
# test_multicore existed in both, so the integration build — the only build that
# checks the two repositories work together — was the one build that could not
# configure:
#
#   add_executable cannot create target "test_crypto" because another target
#   with the same name already exists.
#
# Each repo built fine alone, which is why it went unnoticed. Prefixing is what
# keeps them apart; this rejects a new target that forgets to, at configure time
# and in this repo, rather than as a confusing collision in a different one.
get_property(_eboot_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_t IN LISTS _eboot_test_targets)
    if(NOT _t MATCHES "^eboot_")
        message(FATAL_ERROR
            "Test target '${_t}' is not namespaced. Name it 'eboot_${_t}' so it "
            "cannot collide with a target of the same name in eos when ebuild "
            "builds both together. The add_test(NAME ...) label can stay as it is.")
    endif()
endforeach()
