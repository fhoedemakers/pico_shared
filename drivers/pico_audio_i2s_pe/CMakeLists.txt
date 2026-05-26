if (NOT TARGET pico_audio_i2s)
    add_library(pico_audio_i2s INTERFACE)

    pico_generate_pio_header(pico_audio_i2s ${CMAKE_CURRENT_LIST_DIR}/audio_i2s.pio)

    target_sources(pico_audio_i2s INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/pe_audio_i2s_wrapper.c
        ${CMAKE_CURRENT_LIST_DIR}/pico_extras/audio.cpp
        ${CMAKE_CURRENT_LIST_DIR}/pico_extras/buffer.c
        ${CMAKE_CURRENT_LIST_DIR}/audio_i2s_adapter.c
    )

    target_include_directories(pico_audio_i2s INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}                       # legacy-shaped audio_i2s.h
        ${CMAKE_CURRENT_LIST_DIR}/pico_extras/include   # pico/audio.h, pico/audio_i2s.h, pico/util/buffer.h, pico/sample_conversion.h
    )

    target_link_libraries(pico_audio_i2s INTERFACE
        pico_stdlib
        pico_sync
        hardware_pio
        hardware_dma
        hardware_irq
        hardware_clocks
        tlv320dac3100
        # Same backwards-compatibility set as the legacy driver: other
        # translation units in this project pick these up transitively
        # via pico_audio_i2s. Keeps the dependency surface identical.
        hardware_spi
        hardware_pwm
        hardware_adc
    )
endif()
