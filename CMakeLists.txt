# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Pull in the Pico SDK (local file, not the env-var version)
include(pico_sdk_import.cmake)

project(pico_console C CXX ASM)

# Initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(pico_console
    main.c
    init.c
    snake.c
    spaceship.c
    paint.c
    )

# Enable USB serial output (printf), disable UART serial
pico_enable_stdio_usb(pico_console 1)
pico_enable_stdio_uart(pico_console 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(pico_console)

target_link_libraries(pico_console
    pico_stdlib
    hardware_spi
    hardware_gpio
)

if (PICO_CYW43_SUPPORTED)
    target_link_libraries(pico_console pico_cyw43_arch_none)
endif()