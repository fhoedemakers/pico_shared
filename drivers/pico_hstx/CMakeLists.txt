if (NOT TARGET pico_hstx)

    add_library(pico_hstx INTERFACE)

    target_sources(pico_hstx INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/hstx.c
    )

    target_include_directories(pico_hstx INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    target_link_libraries(pico_hstx INTERFACE
        pico_stdlib
        pico_multicore
        hardware_dma
        pico_sync
    )
endif()
