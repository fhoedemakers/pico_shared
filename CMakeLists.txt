
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
ffwrappers.c
PicoPlusPsram.cpp
)
add_subdirectory(drivers/pico_fatfs)     
add_subdirectory(drivers/pico_audio_i2s)
add_subdirectory(drivers/lwmem)

target_include_directories(pico_shared INTERFACE
        .
)
target_link_libraries(pico_shared INTERFACE pico_fatfs pico_audio_i2s lwmem)

