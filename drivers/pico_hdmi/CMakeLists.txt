if (NOT TARGET pico_hdmi)
add_library(pico_hdmi INTERFACE)
target_sources(pico_hdmi INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/video_output.c
            ${CMAKE_CURRENT_LIST_DIR}/hstx_packet.c
            ${CMAKE_CURRENT_LIST_DIR}/hstx_data_island_queue.c
    )
target_include_directories(pico_hdmi INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}
    )
target_link_libraries(pico_hdmi INTERFACE pico_stdlib hardware_spi hardware_i2c hardware_pwm hardware_adc hardware_pio hardware_dma)
endif()