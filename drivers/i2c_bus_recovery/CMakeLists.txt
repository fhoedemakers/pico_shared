if (NOT TARGET i2c_bus_recovery)
    add_library(i2c_bus_recovery INTERFACE)
    target_sources(i2c_bus_recovery INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/i2c_bus_recovery.c
    )
    target_include_directories(i2c_bus_recovery INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
    )
    target_link_libraries(i2c_bus_recovery INTERFACE
        pico_stdlib
        hardware_gpio
    )
endif()
