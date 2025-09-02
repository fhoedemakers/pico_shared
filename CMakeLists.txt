
cmake_minimum_required(VERSION 3.13)
set(LWMEM_OPTS_FILE ${CMAKE_CURRENT_LIST_DIR}/lwmem_opts_ppp.h)
add_library( pico_shared INTERFACE)
target_sources(pico_shared INTERFACE
menu.cpp
RomLister.cpp
settings.cpp
FrensHelpers.cpp
gamepad.cpp
hid_app.cpp
nespad.cpp
wiipad.cpp
FrensFonts.cpp
ffwrappers.cpp
crc32.cpp
vumeter.cpp
DefaultSS444.c
DefaultSS555.c
#PicoPlusPsram.cpp
)
add_subdirectory(drivers/pico_fatfs)     
add_subdirectory(drivers/pico_audio_i2s)
add_subdirectory(drivers/lwmem)
add_subdirectory(drivers/pico_hstx)
# add_subdirectory(drivers/pico_audio_mcp4822) # SPI Audio using MCP4822 DAC. Works but not used
add_subdirectory(drivers/pico_psram)

target_include_directories(pico_shared INTERFACE
        .
)
pico_generate_pio_header(${projectname} ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)
target_link_libraries(pico_shared 
                      INTERFACE 
                      pico_rand
                      pico_fatfs 
                      pico_audio_i2s 
                      lwmem 
                      pico_hstx 
                      #pico_audio_mcp4822 
                      pico_psram)

