cmake_minimum_required(VERSION 3.13)

# Pull in Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(picoreseau C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialise the Pico SDK
pico_sdk_init()


# Add executable. Default name is the project name, version 0.1
add_executable(picoreseau
    src/picoreseau.cpp     
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
)

ADD_CUSTOM_COMMAND(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
           ${CMAKE_CURRENT_BINARY_DIR}/_version.cpp
    COMMAND ${CMAKE_COMMAND} -P
            ${CMAKE_CURRENT_SOURCE_DIR}/version.cmake)
message(STATUS "CMAKE_CURRENT_BINARY_DIR = ${CMAKE_CURRENT_BINARY_DIR}")
# Generate pio header
#pico_generate_pio_header(picoreseau ${CMAKE_CURRENT_LIST_DIR}/src/picoreseau.pio)

# Add the standard library to the build
target_link_libraries(picoreseau pico_stdlib pico_multicore hardware_pio)

target_compile_options(picoreseau PUBLIC -Wall -Wextra -Wno-unused-function -Wno-unused-parameter)
target_compile_definitions(picoreseau PUBLIC DEBUG N_SD_CARDS=1)

pico_set_program_name(picoreseau "picoreseau")
pico_set_program_version(picoreseau "0.1")

pico_enable_stdio_uart(picoreseau 0)
pico_enable_stdio_usb(picoreseau 1)

target_link_libraries(picoreseau
    hardware_pio
)

pico_add_extra_outputs(picoreseau)

add_custom_target(dist 
    COMMAND
    ${CMAKE_COMMAND} -E tar "cfv" "${CMAKE_CURRENT_BINARY_DIR}/release.zip" --format=zip
       "${CMAKE_CURRENT_BINARY_DIR}/picoreseau.uf2"
       "${CMAKE_CURRENT_BINARY_DIR}/picoreseau.hex"
       "${CMAKE_CURRENT_BINARY_DIR}/picoreseau.bin"
       "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)