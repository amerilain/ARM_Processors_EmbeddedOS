cmake_minimum_required(VERSION 3.12)

# Name project
set(ProjectName rp2040-freertos-cpp-template)

# Set any variables required for importing libraries
set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS-KernelV10.6.2)

# Import those libraries
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

# Include the directory where FreeRTOSConfig.h is located
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Define project
project(${ProjectName})

# Initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# Specify the executable and source files
add_executable(${ProjectName}
        src/main.cpp
        src/PicoOsUart.cpp
        ${FREERTOS_KERNEL_PATH}/portable/MemMang/heap_4.c  # Add the heap memory management file
)

# Link against FreeRTOS Kernel and the necessary Pico SDK libraries
target_link_libraries(${ProjectName}
        pico_stdlib
        hardware_gpio
        hardware_uart
        FreeRTOS-Kernel
)

# Enable UART output over USB for debugging
pico_enable_stdio_uart(${ProjectName} 1)
pico_enable_stdio_usb(${ProjectName} 1)

# Create the UF2 binary output for the Pico board
pico_add_extra_outputs(${ProjectName})