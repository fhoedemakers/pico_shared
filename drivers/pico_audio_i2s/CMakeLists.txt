if (NOT TARGET pico_audio_i2s)
add_library(pico_audio_i2s INTERFACE)
pico_generate_pio_header(pico_audio_i2s ${CMAKE_CURRENT_LIST_DIR}/audio_i2s.pio)
target_sources(pico_audio_i2s INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/audio_i2s.c
    )
target_include_directories(pico_audio_i2s INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}
    )
target_link_libraries(pico_audio_i2s INTERFACE pico_stdlib hardware_spi hardware_i2c hardware_pwm hardware_adc hardware_pio hardware_dma)
endif()