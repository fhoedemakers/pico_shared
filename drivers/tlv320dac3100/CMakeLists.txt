if (NOT TARGET tlv320dac3100)
    add_library(tlv320dac3100 INTERFACE)
    target_sources(tlv320dac3100 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/tlv320dac3100.c
    )
    target_include_directories(tlv320dac3100 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
    )
    target_link_libraries(tlv320dac3100 INTERFACE
        pico_stdlib
        hardware_i2c
        hardware_gpio
    )
endif()
