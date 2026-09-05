cmake_minimum_required(VERSION 3.20)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/cmake/avr-gcc-toolchain.cmake"
    CACHE PATH "Toolchain file")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(stpalpha C CXX)

set(MCU atmega32u4)
set(F_CPU 16000000UL)

set(AVR_COMPILE_OPTIONS
    -mmcu=${MCU}
    -DF_CPU=${F_CPU}
    -Os
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

set(AVR_WARNING_OPTIONS
    -Wall
    -Wextra
    -Wpedantic
)

# avr-libc's crt provides the vector table, stack setup and .data/.bss init;
# the HAL hooks .init8 for peripheral bring-up (src/stpalpha/hal/setup.c).
set(AVR_LINK_OPTIONS
    -mmcu=${MCU}
    -Wl,--gc-sections
)

set(HAL_SOURCES
    src/stpalpha/hal/setup.c
)

# add_avr_executable(<name> <main source> [EXTRA_SOURCES ...])
# Every image links the board HAL so the timers and ISRs exist before main.
function(add_avr_executable TARGET_NAME SOURCE_FILE)
    cmake_parse_arguments(ARG "" "" "EXTRA_SOURCES" ${ARGN})
    add_executable(${TARGET_NAME}.elf
        ${SOURCE_FILE}
        ${ARG_EXTRA_SOURCES}
        ${HAL_SOURCES}
    )

    target_include_directories(${TARGET_NAME}.elf
        PRIVATE ${PROJECT_SOURCE_DIR}/include
    )

    target_compile_features(${TARGET_NAME}.elf PRIVATE cxx_std_17)

    target_compile_options(${TARGET_NAME}.elf
        PRIVATE ${AVR_COMPILE_OPTIONS} ${AVR_WARNING_OPTIONS}
    )

    target_link_options(${TARGET_NAME}.elf
        PRIVATE ${AVR_LINK_OPTIONS}
    )

    add_custom_command(TARGET ${TARGET_NAME}.elf POST_BUILD
        COMMAND avr-objcopy -O ihex -R .eeprom
            $<TARGET_FILE:${TARGET_NAME}.elf> ${TARGET_NAME}.hex
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating ${TARGET_NAME}.hex"
    )

    add_custom_command(TARGET ${TARGET_NAME}.elf POST_BUILD
        COMMAND avr-size $<TARGET_FILE:${TARGET_NAME}.elf>
        COMMENT "Size of ${TARGET_NAME}.elf"
    )
endfunction()

set(LCD_SOURCES src/stpalpha/hal/lcd.c)

# The production image.
add_avr_executable(firmware src/stpalpha/main.cpp EXTRA_SOURCES ${LCD_SOURCES})

# Build/simulation smoke image: prints on USART1 (tests/system/test_firmware.py).
add_avr_executable(hello_world tests/avr/hello_world.cpp)

# Hardware test harnesses, one image each for targeted diagnosis
# (docs/runbooks/hardware-tests.md).
add_avr_executable(test_buttons_leds tests/avr/test_buttons_leds.cpp)
add_avr_executable(test_buzzer tests/avr/test_buzzer.cpp EXTRA_SOURCES ${LCD_SOURCES})
add_avr_executable(test_encoder tests/avr/test_encoder.cpp EXTRA_SOURCES ${LCD_SOURCES})
add_avr_executable(test_lcd tests/avr/test_lcd.cpp EXTRA_SOURCES ${LCD_SOURCES})
add_avr_executable(test_leds_pins tests/avr/test_leds_pins.cpp EXTRA_SOURCES ${LCD_SOURCES})
add_avr_executable(test_timer0 tests/avr/test_timer0.cpp EXTRA_SOURCES ${LCD_SOURCES})

# Uploading is owned by scripts/flash.py (bootloader attach + avrdude);
# keep cmake to building images.

# ctest smoke: the hello_world image must print on USART1 under simavr
# (simavr echoes the UART on stderr, hence 2>&1).
# (tests/system/test_firmware.py runs the same check from pytest; this one
# exists so `ctest --test-dir build` is meaningful on its own.)
enable_testing()
add_test(NAME hello_world_simavr
    COMMAND sh -c "timeout 3 simavr -m ${MCU} -f ${F_CPU} hello_world.elf 2>&1 | grep -q 'Hello world from ATmega32U4'"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_target(sim
    COMMAND simavr -m ${MCU} -f ${F_CPU} firmware.elf
    DEPENDS firmware.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

file(GLOB_RECURSE FIRMWARE_SOURCES
    "${PROJECT_SOURCE_DIR}/src/stpalpha/*.c"
    "${PROJECT_SOURCE_DIR}/src/stpalpha/*.cpp"
    "${PROJECT_SOURCE_DIR}/include/*.h"
    "${PROJECT_SOURCE_DIR}/tests/avr/*.cpp"
)

find_program(RUN_CLANG_TIDY_EXE NAMES run-clang-tidy run-clang-tidy-18 run-clang-tidy-17)
if(RUN_CLANG_TIDY_EXE)
    add_custom_target(tidy
        COMMAND ${RUN_CLANG_TIDY_EXE} -p=${CMAKE_BINARY_DIR} ${FIRMWARE_SOURCES}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Running clang-tidy"
        VERBATIM
    )
else()
    message(WARNING "Could not find run-clang-tidy.")
endif()

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${FIRMWARE_SOURCES}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Formatting firmware sources"
        VERBATIM
    )
else()
    message(WARNING "Could not find clang-format.")
endif()
