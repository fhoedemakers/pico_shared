if (NOT TARGET pico_audio_mcp4822)

    add_library(pico_audio_mcp4822 INTERFACE)

    target_sources(pico_audio_mcp4822 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/mcp4822.c
        ${CMAKE_CURRENT_LIST_DIR}/myringbuffer.c
    )

    target_include_directories(pico_audio_mcp4822 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    target_link_libraries(pico_audio_mcp4822 INTERFACE
        pico_stdlib
        hardware_spi 
        hardware_irq
        hardware_timer
    )
endif()
