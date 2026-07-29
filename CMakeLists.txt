cmake_minimum_required(VERSION 3.13)

# Pull in the Pico SDK (must set PICO_SDK_PATH env var, or pass
# -DPICO_SDK_PATH=/path/to/pico-sdk when running cmake)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_lcd_test C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(pico_lcd_test
    main.c
    init.c
)

# Enable USB serial output (printf), disable UART serial
pico_enable_stdio_usb(pico_lcd_test 1)
pico_enable_stdio_uart(pico_lcd_test 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(pico_lcd_test)

target_link_libraries(pico_lcd_test
    pico_stdlib
    hardware_spi
    hardware_gpio
)
