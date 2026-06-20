# BootPartition.cmake - Shared flash memory map for the emuLoader bootloader.
#
# The pico_emuLoader bootloader lives at the very start of flash and stays
# resident; emulators are relinked to run from the application partition right
# after it, so the bootloader can flash an emulator and jump to it while the
# bootloader survives every reset / power-cycle.
#
# This file is the SINGLE SOURCE OF TRUTH for the map. It is included by:
#   - the bootloader (pico_emuLoader/CMakeLists.txt) -> frens_link_as_bootloader()
#   - every emulator built with -DBUILD_FOR_BOOTLOADER=ON -> frens_offset_for_bootloader()
# and the same numbers are passed to the bootloader's C code (boot_config.h) as
# compile definitions, so the linker and the loader can never disagree.
#
# Flash layout (Adafruit Fruit Jam, 16 MB):
#
#   0x10000000  +-----------------------------+  <- bootrom always boots this
#               |   Bootloader (emuLoader)    |     image (the menu/flasher).
#               |        1 MB                  |
#   0x10100000  +-----------------------------+  <- FRENS_APP_BASE
#               |   Application partition      |     emulator UF2s land here.
#               |        15 MB                 |     the bootloader jumps here.
#   0x11000000  +-----------------------------+
#
# Override on the command line for other boards, e.g.:
#   cmake -DFRENS_FLASH_TOTAL=0x800000 ...   # 8 MB board

if(DEFINED _FRENS_BOOTPARTITION_INCLUDED)
    return()
endif()
set(_FRENS_BOOTPARTITION_INCLUDED 1)

set(FRENS_XIP_BASE        "0x10000000" CACHE STRING "RP2350 XIP flash base")
set(FRENS_BOOTLOADER_SIZE "0x100000"   CACHE STRING "Bytes reserved for the resident bootloader (1 MB)")
set(FRENS_FLASH_TOTAL     "0x1000000"  CACHE STRING "Total external flash on the board (Fruit Jam = 16 MB)")

# Derived addresses.
math(EXPR FRENS_APP_BASE "${FRENS_XIP_BASE} + ${FRENS_BOOTLOADER_SIZE}" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR FRENS_APP_SIZE  "${FRENS_FLASH_TOTAL} - ${FRENS_BOOTLOADER_SIZE}" OUTPUT_FORMAT HEXADECIMAL)

# ---------------------------------------------------------------------------
# _frens_relink_flash(<target> <origin_hex> <length_hex>)
#
# Copy the SDK's default RP2350 memory map and rewrite only the FLASH(rx)
# ORIGIN/LENGTH line, then point <target> at it. Adapted from the proven
# rp2350-uf2-bootloader/cmake/patch_memmap.cmake. Keeps us compatible with SDK
# updates (everything else in the script is untouched).
#
# Must be called AFTER the target exists and AFTER pico_sdk_init().
# ---------------------------------------------------------------------------
function(_frens_relink_flash TARGET ORIGIN LENGTH)
    set(_candidates
        "${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2350/memmap_default.ld"
        "${PICO_SDK_PATH}/src/rp2_common/pico_standard_link/memmap_default.ld"
    )
    set(_src_ld "")
    foreach(_c IN LISTS _candidates)
        if(EXISTS "${_c}")
            set(_src_ld "${_c}")
            break()
        endif()
    endforeach()
    if(_src_ld STREQUAL "")
        file(GLOB_RECURSE _found
             "${PICO_SDK_PATH}/src/rp2_common/*rp2350*/memmap_default.ld")
        if(_found)
            list(GET _found 0 _src_ld)
        endif()
    endif()
    if(_src_ld STREQUAL "")
        message(FATAL_ERROR
            "BootPartition: could not find an RP2350 memmap_default.ld under "
            "${PICO_SDK_PATH}. Build for the rp2350 platform with SDK >= 2.0.0.")
    endif()

    file(READ "${_src_ld}" _ld_text)
    string(REGEX REPLACE
        "FLASH\\(rx\\)[^\n]*"
        "FLASH(rx) : ORIGIN = ${ORIGIN}, LENGTH = ${LENGTH}"
        _ld_text "${_ld_text}")

    set(_out_ld "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_frens_memmap.ld")
    file(WRITE "${_out_ld}" "${_ld_text}")
    message(STATUS "BootPartition: ${TARGET} -> ORIGIN=${ORIGIN} LENGTH=${LENGTH} (from ${_src_ld})")
    pico_set_linker_script(${TARGET} "${_out_ld}")
endfunction()

# Link the bootloader into the reserved boot region at the start of flash.
function(frens_link_as_bootloader TARGET)
    _frens_relink_flash(${TARGET} ${FRENS_XIP_BASE} ${FRENS_BOOTLOADER_SIZE})
endfunction()

# Link an emulator into the application partition (its UF2 will then target the
# partition, and the bootloader writes it verbatim and jumps there).
function(frens_offset_for_bootloader TARGET)
    _frens_relink_flash(${TARGET} ${FRENS_APP_BASE} ${FRENS_APP_SIZE})
endfunction()
