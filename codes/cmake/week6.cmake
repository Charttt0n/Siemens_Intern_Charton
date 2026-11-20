# Week-6 targets. Week-1..5 targets stay defined in the root CMakeLists.
set(MBEDTLS_DIR ${CMAKE_SOURCE_DIR}/tests/third_party/mbedtls)
set(MBEDTLS_SRCS
    ${MBEDTLS_DIR}/library/sha256.c
    ${MBEDTLS_DIR}/library/md.c
    ${MBEDTLS_DIR}/library/rsa.c
    ${MBEDTLS_DIR}/library/rsa_alt_helpers.c
    ${MBEDTLS_DIR}/library/bignum.c
    ${MBEDTLS_DIR}/library/bignum_core.c
    ${MBEDTLS_DIR}/library/bignum_mod.c
    ${MBEDTLS_DIR}/library/bignum_mod_raw.c
    ${MBEDTLS_DIR}/library/constant_time.c
    ${MBEDTLS_DIR}/library/oid.c
    ${MBEDTLS_DIR}/library/asn1parse.c
    ${MBEDTLS_DIR}/library/asn1write.c
    ${MBEDTLS_DIR}/library/platform.c
    ${MBEDTLS_DIR}/library/platform_util.c
    ${MBEDTLS_DIR}/library/memory_buffer_alloc.c)
set_source_files_properties(${MBEDTLS_SRCS} PROPERTIES
    COMPILE_FLAGS "-Os -Wno-error -Wno-unused-parameter")

set(WEEK6_INCLUDES
    src/week6/nolibc src/week6 src/week5 src/week4 src/week3 src/week2
    ${MBEDTLS_DIR}/include ${MBEDTLS_DIR}/library)
set(WEEK6_DEFS MBEDTLS_CONFIG_FILE="week6_mbedtls_config.h")
set(WEEK6_KEY_DIR "${CMAKE_SOURCE_DIR}/build/test-signing")
add_custom_target(week6-lab-key
    COMMAND python3 "${CMAKE_SOURCE_DIR}/scripts/lab_signing_key.py"
    BYPRODUCTS "${WEEK6_KEY_DIR}/week6_lab_public_key.h"
    VERBATIM)
set(WEEK6_BL_SRCS
    src/week3/startup.S src/week3/uart.c src/week3/flash.c src/week2/board.c
    src/week6/main.c src/week6/boot_config.c src/week6/secure_image.c
    src/week6/watchdog.c src/week6/update.c src/week6/libc_min.c
    src/week5/boot_meta.c src/week5/crc16.c src/week5/ymodem.c src/week5/timebase.c
    src/week4/image.c src/week4/crc32.c src/week4/handoff.S
    ${MBEDTLS_SRCS})

function(configure_week6 target script)
    set(optimization -Og)
    if(ARGC GREATER 2)
        set(optimization ${ARGV2})
    endif()
    target_include_directories(${target} PRIVATE ${WEEK6_INCLUDES})
    if(target MATCHES "^bootloader-week[67]")
        add_dependencies(${target} week6-lab-key)
        target_include_directories(${target} PRIVATE "${WEEK6_KEY_DIR}")
    endif()
    target_compile_definitions(${target} PRIVATE ${WEEK6_DEFS})
    target_compile_options(${target} PRIVATE -mcpu=cortex-m7 -mthumb
        -mfloat-abi=soft -g3 ${optimization} -ffreestanding -fno-builtin
        -ffunction-sections -fdata-sections -Wall -Wextra -Werror)
    target_link_options(${target} PRIVATE -mcpu=cortex-m7 -mthumb
        -mfloat-abi=soft -nostdlib -Wl,--gc-sections
        -Wl,-Map=${CMAKE_BINARY_DIR}/${target}.map -T${CMAKE_SOURCE_DIR}/${script})
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/${script})
    # libgcc must come after objects; -lgcc in target_link_options is placed too early.
    target_link_libraries(${target} PRIVATE gcc)
endfunction()

add_executable(bootloader-week6.elf ${WEEK6_BL_SRCS})
configure_week6(bootloader-week6.elf src/week6/linker.ld)

function(add_week6_app target version extra)
    add_executable(${target}
        src/week4/app/startup.S src/week6/app/main.c src/week2/uart.c
        src/week6/app_confirm.c src/week6/boot_config.c src/week6/watchdog.c
        src/week6/libc_min.c src/week3/flash.c src/week4/crc32.c
        src/week5/boot_meta.c)
    target_include_directories(${target} PRIVATE src/week6/nolibc src/week6 src/week5 src/week3 src/week2 src/week4)
    target_compile_definitions(${target} PRIVATE APP_VERSION_STRING="v${version}" ${extra})
    configure_stage4(${target} src/week4/app/linker.ld)
endfunction()
add_week6_app(application-week6-v1.elf 1 APP_WEEK6_CONFIRM)
add_week6_app(application-week6-v2.elf 2 APP_WEEK6_CONFIRM)
add_week6_app(application-week6-v3.elf 3 APP_WEEK6_CONFIRM)
add_week6_app(application-week6-hang.elf hang APP_WEEK6_HANG)
