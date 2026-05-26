if (NOT TARGET pico_audio_i2s)
add_library(pico_audio_i2s INTERFACE)
pico_generate_pio_header(pico_audio_i2s ${CMAKE_CURRENT_LIST_DIR}/audio_i2s.pio)
target_sources(pico_audio_i2s INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/audio_i2s.c
    )
target_include_directories(pico_audio_i2s INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}
    )
target_link_libraries(pico_audio_i2s INTERFACE
    pico_stdlib
    hardware_pio
    hardware_dma
    tlv320dac3100
    # Note: hardware_spi/hardware_pwm/hardware_adc are linked here for
    # backwards compatibility — the rest of the project picks them up
    # transitively from pico_audio_i2s. Remove only after auditing callers.
    hardware_spi
    hardware_pwm
    hardware_adc
)
endif()
