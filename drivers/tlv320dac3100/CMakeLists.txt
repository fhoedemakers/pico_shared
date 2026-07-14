if (NOT TARGET tlv320dac3100)
    if (NOT TARGET i2c_bus_recovery)
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../i2c_bus_recovery i2c_bus_recovery)
    endif()
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
        i2c_bus_recovery
    )
endif()
