if (NOT TARGET pico_psram)

    add_library(pico_psram INTERFACE)

    target_sources(pico_psram INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/PicoPlusPsram.cpp
        ${CMAKE_CURRENT_LIST_DIR}/SetupPsram.c
    )

    target_include_directories(pico_psram INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    target_link_libraries(pico_psram INTERFACE
        pico_stdlib
        pico_multicore
        hardware_dma
        pico_sync
    )
endif()
