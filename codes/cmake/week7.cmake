# Production and debug/fault targets use the same security path. Week 6 is
# retained as a reproducible unoptimized baseline; no security check is cut.
set(WEEK7_SRCS ${WEEK6_BL_SRCS})
list(REMOVE_ITEM WEEK7_SRCS src/week6/main.c src/week4/crc32.c)
list(APPEND WEEK7_SRCS src/week7/main.c src/week7/control.c src/week7/crc32.c)
function(configure_week7 target)
    configure_week6(${target} src/week6/linker.ld -Os)
    target_include_directories(${target} PRIVATE src/week7)
    # Retain debug info for postmortem GDB; exactly one optimization flag.
endfunction()
# Share LTO settings between release and fault builds. Keep the flash adapter
# and transaction boundaries as regular objects for ld --wrap and GDB probes.
option(BOOT_CRYPTO_LTO "Optimize the release signature verifier at link time" ON)
if(BOOT_CRYPTO_LTO)
    set_property(SOURCE ${MBEDTLS_SRCS} src/week6/secure_image.c APPEND PROPERTY
        COMPILE_OPTIONS "$<$<BOOL:$<TARGET_PROPERTY:BOOT_CRYPTO_LTO>>:-flto>")
endif()
add_executable(bootloader-week7.elf ${WEEK7_SRCS})
configure_week7(bootloader-week7.elf)
add_executable(bootloader-week7-debug.elf ${WEEK7_SRCS})
configure_week6(bootloader-week7-debug.elf src/week6/linker.ld -Og)
target_include_directories(bootloader-week7-debug.elf PRIVATE src/week7)
add_executable(bootloader-week7-fault.elf ${WEEK7_SRCS} tests/week7/fault.c)
configure_week7(bootloader-week7-fault.elf)
target_link_options(bootloader-week7-fault.elf PRIVATE
    -Wl,--wrap=flash_program -Wl,--wrap=flash_erase -Wl,--wrap=flash_read)
if(BOOT_CRYPTO_LTO)
    foreach(target bootloader-week7.elf bootloader-week7-fault.elf)
        set_property(TARGET ${target} PROPERTY BOOT_CRYPTO_LTO TRUE)
        target_link_options(${target} PRIVATE -flto -Os)
    endforeach()
endif()
# Reproduce the pre-fix activation gap, without changing Week-6 production.
add_executable(bootloader-week6-review-fault.elf ${WEEK6_BL_SRCS} tests/week7/fault.c)
configure_week6(bootloader-week6-review-fault.elf src/week6/linker.ld)
target_link_options(bootloader-week6-review-fault.elf PRIVATE
    -Wl,--wrap=flash_program -Wl,--wrap=flash_erase -Wl,--wrap=flash_read)
function(add_week7_app target version extra)
    add_executable(${target} src/week4/app/startup.S src/week6/app/main.c
        src/week2/uart.c src/week7/app_confirm.c src/week6/boot_config.c
        src/week6/watchdog.c src/week6/libc_min.c src/week3/flash.c
        src/week7/crc32.c src/week5/boot_meta.c)
    configure_week7(${target})
    # Application uses its own link map, not the bootloader's map.
    set_property(TARGET ${target} PROPERTY LINK_OPTIONS
        -mcpu=cortex-m7 -mthumb -mfloat-abi=soft -nostdlib -Wl,--gc-sections
        -Wl,-Map=${CMAKE_BINARY_DIR}/${target}.map
        -T${CMAKE_SOURCE_DIR}/src/week4/app/linker.ld)
    set_property(TARGET ${target} PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/src/week4/app/linker.ld)
    target_compile_definitions(${target} PRIVATE APP_VERSION_STRING="v${version}" ${extra})
endfunction()
add_week7_app(application-week7-v1.elf 1 APP_WEEK6_CONFIRM)
add_week7_app(application-week7-v2.elf 2 APP_WEEK6_CONFIRM)
add_week7_app(application-week7-v3.elf 3 APP_WEEK6_CONFIRM)
add_week7_app(application-week7-hang.elf hang APP_WEEK6_HANG)

add_executable(unity-week7.elf tests/week7/test_target.c
    tests/third_party/unity/src/unity.c tests/tx_fault_state.c
    src/week3/startup.S src/week3/uart.c src/week2/board.c src/week5/timebase.c
    src/week6/watchdog.c src/week7/app_confirm.c src/week6/boot_config.c
    src/week3/flash.c src/week5/boot_meta.c src/week7/crc32.c src/week6/libc_min.c)
configure_week7(unity-week7.elf)
target_include_directories(unity-week7.elf PRIVATE tests/week7 tests tests/third_party/unity/src)
target_compile_definitions(unity-week7.elf PRIVATE UNITY_INCLUDE_CONFIG_H UART_TX_FAULT_TEST)
