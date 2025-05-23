
cmake_minimum_required(VERSION 3.13)
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
ext_audio.c
)
add_subdirectory(drivers/pico_fatfs)     
add_subdirectory(drivers/audio/pico_audio_i2s)
add_subdirectory(drivers/audio/pico_audio) 
add_subdirectory(drivers/audio/pico_util_buffer) 
target_include_directories(pico_shared INTERFACE
        .
)
target_link_libraries(pico_shared INTERFACE pico_fatfs pico_audio pico_audio_i2s pico_util_buffer)

