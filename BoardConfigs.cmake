# BoardConfigs.cmake
# 
# This file defines hardware configuration, board type, and pin assignments for supported boards.
# It is included from the main CMakeLists.txt of the project.
#
# Usage:
#   - Set HW_CONFIG to select the hardware configuration.
#   - Set the PICO_SDK_PATH environment variable to the Pico SDK location.
#   - Set the PICO_PIO_USB_PATH environment variable to the Pico-PIO-USB library location if PIO USB is needed.
#
# Notes:
#   - The latest master branch of TinyUSB is required for PIO USB support and for Adafruit Metro RP2350.
#   - The latest branch of https://github.com/sekigon-gonnoc/Pico-PIO-USB is required for PIO USB support.
#   - PIO USB is only supported on boards with sufficient memory (e.g., RP2350).
#
# check if $ENV{PICO_SDK_PATH} is set and points to a valid directory
if (NOT DEFINED ENV{PICO_SDK_PATH} OR NOT IS_DIRECTORY $ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATH environment variable is not set or points to an invalid directory. Please set it to the path of the Pico SDK.")
endif()
set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})

# set PICO_PIO_USB_PATH from environment variable
if(NOT DEFINED PICO_PIO_USB_PATH)
  if(DEFINED ENV{PICO_PIO_USB_PATH})
    set(PICO_PIO_USB_PATH $ENV{PICO_PIO_USB_PATH})
  endif()
endif()
# check if PICO_PIO_USB_PATH is set and points to a valid directory
if(DEFINED PICO_PIO_USB_PATH)
    if(NOT EXISTS ${PICO_PIO_USB_PATH}/src/pio_usb.c)
        message(FATAL_ERROR "Pico PIO usb repo not found in ${PICO_PIO_USB_PATH}. Please fetch the repo")
    endif()
else()
    # disable PIO USB support if PICO_PIO_USB_PATH is not set
    set(ENABLE_PIO_USB 0 CACHE BOOL "Enable PIO USB support")
endif()

# Default tinyusb board type
set(BOARD pico_sdk)

if ( HW_CONFIG EQUAL 1 )
	# This default Config is for Pimoroni Pico DV Demo Base, note uart is disabled because gpio 1 is used for NES controller
	set(DVICONFIG "dviConfig_PimoroniDemoDVSock" CACHE STRING
	  "Select a default pin configuration from common_dvi_pin_configs.h")
    set(LED_GPIO_PIN "0" CACHE STRING "Select the GPIO pin for LED")         # use 0 for onboard LED (PICO/PICO_W)
	set(SD_CS "22" CACHE STRING "Specify the Chip Select GPIO pin for the SD card")
	set(SD_SCK "5" CACHE STRING "Specify de Clock GPIO pin for the SD card")
	set(SD_MOSI "18" CACHE STRING "Select the Master Out Slave In GPIO pin for the SD card")
	set(SD_MISO "19" CACHE STRING "Select the Master In Slave Out GPIO pin for the SD card")
    set(SD_SPI "spi0" CACHE STRING "Select the SPI bus for SD card") 
	set(NES_CLK "14" CACHE STRING "Select the Clock GPIO pin for NES controller")
	set(NES_DATA "15" CACHE STRING "Select the Data GPIO pin for NES controller")
	set(NES_LAT "16" CACHE STRING "Select the Latch GPIO pin for NES controller")
    set(NES_PIO "pio0" CACHE STRING "Select the PIO for NES controller")
    set(NES_CLK_1 "1" CACHE STRING "Select the Clock GPIO pin for second NES controller")
	set(NES_DATA_1 "21" CACHE STRING "Select the Data GPIO pin for second NES controller")
	set(NES_LAT_1 "20" CACHE STRING "Select the Latch GPIO pin for second NES controller")
    set(NES_PIO_1 "pio1" CACHE STRING "Select the PIO for second NES controller")
	set(WII_SDA "-1" CACHE STRING "Select the SDA GPIO pin for Wii Classic controller")
	set(WII_SCL "-1" CACHE STRING "Select the SCL GPIO pin for Wii Classic controller")
    set(WIIPAD_I2C "i2c1" CACHE STRING "Select the I2C bus for Wii Classic controller")
    set(UART_ENABLED 0 CACHE STRING "Enable UART output") # Disable UART output for Pimoroni Pico DV Demo Base
    set(USE_I2S_AUDIO 1 CACHE STRING "Enable I2S audio output") # Enable I2S audio output for Pimoroni Pico DV Demo Base
    set(PICO_AUDIO_I2S_DATA_PIN 26 CACHE STRING "Select the GPIO pin for I2S data")
    set(PICO_AUDIO_I2S_CLOCK_PIN_BASE 27 CACHE STRING "Select the GPIO pin for I2S clock")
    set(PICO_AUDIO_I2S_PIO 1 CACHE STRING "Select the PIO for I2S audio output")
    set(PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0 CACHE STRING "Set to 1 if the I2S clock pins are swapped")
    set(PIO_USB_USE_PIO 2 CACHE BOOL "Select the PIO used for PIO-USB")
    set(PIO_DP_PLUS_PIN -1 CACHE STRING "PIO USB DP pin.")
elseif ( HW_CONFIG EQUAL 2 )
	# --------------------------------------------------------------------
	# Alternate config for use with different SDcard reader and HDMI board
    # Use also for Printed Circuit Board (PCB) version.
	# --------------------------------------------------------------------
	# Adafruit DVI Breakout For HDMI Source Devices https://www.adafruit.com/product/4984
	set(DVICONFIG "dviConfig_PicoDVISock" CACHE STRING
	  "Select a default pin configuration from common_dvi_pin_configs.h")
    set(LED_GPIO_PIN "0" CACHE STRING "Select the GPIO pin for LED")       # use 0 for onboard LED (PICO/PICO_W)
	# Adafruit Micro-SD breakout board+ https://www.adafruit.com/product/254 
	set(SD_CS "5" CACHE STRING "Specify the Chip Select GPIO pin for the SD card")
	set(SD_SCK "2" CACHE STRING "Specify de Clock GPIO pin for the SD card")
	set(SD_MOSI "3" CACHE STRING "Select the Master Out Slave In GPIO pin for the SD card")
	set(SD_MISO "4" CACHE STRING "Select the Master In Slave Out GPIO pin for the SD card")
    set(SD_SPI "spi0" CACHE STRING "Select the SPI bus for SD card") 
	set(NES_CLK "6" CACHE STRING "Select the Clock GPIO pin for NES controller")
	set(NES_DATA "7" CACHE STRING "Select the Data GPIO pin for NES controller")
	set(NES_LAT "8" CACHE STRING "Select the Latch GPIO pin for NES controller")
    set(NES_PIO "pio0" CACHE STRING "Select the PIO for NES controller")
    set(NES_CLK_1 "9" CACHE STRING "Select the Clock GPIO pin for second NES controller")
	set(NES_DATA_1 "10" CACHE STRING "Select the Data GPIO pin for second NES controller")
	set(NES_LAT_1 "11" CACHE STRING "Select the Latch GPIO pin for second NES controller")
    set(NES_PIO_1 "pio1" CACHE STRING "Select the PIO for second NES controller")
	set(WII_SDA "-1" CACHE STRING "Select the SDA GPIO pin for Wii Classic controller")
	set(WII_SCL "-1" CACHE STRING "Select the SCL GPIO pin for Wii Classic controller")
    set(WIIPAD_I2C "i2c1" CACHE STRING "Select the I2C bus for Wii Classic controller")
	set(UART_ENABLED 1 CACHE STRING "Enable UART output") 
    set(USE_I2S_AUDIO 0 CACHE STRING "Enable I2S audio output") # Enable I2S audio output for Pimoroni Pico DV Demo Base
    set(PICO_AUDIO_I2S_DATA_PIN -1 CACHE STRING "Select the GPIO pin for I2S data")
    set(PICO_AUDIO_I2S_CLOCK_PIN_BASE -1 CACHE STRING "Select the GPIO pin for I2S clock")
    set(PICO_AUDIO_I2S_PIO 1 CACHE STRING "Select the PIO for I2S audio output")
    set(PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0 CACHE STRING "Set to 1 if the I2S clock pins are swapped")
    set(PIO_USB_USE_PIO 2 CACHE BOOL "Select the PIO used for PIO-USB")
    # connect the DP+ pin to GPIO 20, DP- will be GPIO 21
    set(PIO_DP_PLUS_PIN 20 CACHE STRING "PIO USB DP pin.")
elseif ( HW_CONFIG EQUAL 3 )
	# --------------------------------------------------------------------
	# Alternate config for use with Adafruit Feather RP2040 DVI + SD Wing
	# --------------------------------------------------------------------
	set(DVICONFIG "dviConfig_AdafruitFeatherDVI" CACHE STRING
	  "Select a default pin configuration from common_dvi_pin_configs.h")
    set(LED_GPIO_PIN "13" CACHE STRING "Select the GPIO pin for LED")   # Adafruit Feather RP2040 onboard LED
	set(SD_CS "10" CACHE STRING "Specify the Chip Select GPIO pin for the SD card")
	set(SD_SCK "14" CACHE STRING "Specify de Clock GPIO pin for the SD card")
	set(SD_MOSI "15" CACHE STRING "Select the Master Out Slave In GPIO pin for the SD card")
	set(SD_MISO "8" CACHE STRING "Select the Master In Slave Out GPIO pin for the SD card")
    set(SD_SPI "spi1" CACHE STRING "Select the SPI bus for SD card")
	set(NES_CLK "5" CACHE STRING "Select the Clock GPIO pin for NES controller")
	set(NES_DATA "6" CACHE STRING "Select the Data GPIO pin for NES controller")
	set(NES_LAT "9" CACHE STRING "Select the Latch GPIO pin for NES controller")
    set(NES_PIO "pio0" CACHE STRING "Select the PIO for NES controller")
    set(NES_CLK_1 "26" CACHE STRING "Select the Clock GPIO pin for second NES controller")
	set(NES_DATA_1 "28" CACHE STRING "Select the Data GPIO pin for second NES controller")
	set(NES_LAT_1 "27" CACHE STRING "Select the Latch GPIO pin for second NES controller")
    set(NES_PIO_1 "pio1" CACHE STRING "Select the PIO for second NES controller")
	set(WII_SDA "2" CACHE STRING "Select the SDA GPIO pin for Wii Classic controller")
	set(WII_SCL "3" CACHE STRING "Select the SCL GPIO pin for Wii Classic controller")
    set(WIIPAD_I2C "i2c1" CACHE STRING "Select the I2C bus for Wii Classic controller")
	set(UART_ENABLED 1 CACHE STRING "Enable UART output")
    set(USE_I2S_AUDIO 0 CACHE STRING "Enable I2S audio output") 
    set(PICO_AUDIO_I2S_DATA_PIN -1 CACHE STRING "Select the GPIO pin for I2S data")
    set(PICO_AUDIO_I2S_CLOCK_PIN_BASE -1 CACHE STRING "Select the GPIO pin for I2S clock")
    set(PICO_AUDIO_I2S_PIO 1 CACHE STRING "Select the PIO for I2S audio output")
    set(PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0 CACHE STRING "Set to 1 if the I2S clock pins are swapped")
    set(PIO_USB_USE_PIO 2 CACHE BOOL "Select the PIO used for PIO-USB")
    set(PIO_DP_PLUS_PIN -1 CACHE STRING "PIO USB DP pin.")
elseif ( HW_CONFIG EQUAL 4 )
    # --------------------------------------------------------------------
	# Alternate config for use with Waveshare RP2040-PiZero
	# --------------------------------------------------------------------
	set(DVICONFIG "dviConfig_WaveShareRp2040" CACHE STRING
    "Select a default pin configuration from common_dvi_pin_configs.h")
    set(LED_GPIO_PIN "-1" CACHE STRING "Select the GPIO pin for LED")   # No onboard LED for Waveshare RP2040-PiZero
    set(SD_CS "21" CACHE STRING "Specify the Chip Select GPIO pin for the SD card")
    set(SD_SCK "18" CACHE STRING "Specify de Clock GPIO pin for the SD card")
    set(SD_MOSI "19" CACHE STRING "Select the Master Out Slave In GPIO pin for the SD card")
    set(SD_MISO "20" CACHE STRING "Select the Master In Slave Out GPIO pin for the SD card")
    set(NES_CLK "5" CACHE STRING "Select the Clock GPIO pin for NES controller")
    set(SD_SPI "spi0" CACHE STRING "Select the SPI bus for SD card")
    set(NES_DATA "6" CACHE STRING "Select the Data GPIO pin for NES controller")
    set(NES_LAT "9" CACHE STRING "Select the Latch GPIO pin for NES controller")
    set(NES_PIO "pio0" CACHE STRING "Select the PIO for NES controller")
    set(NES_CLK_1 "10" CACHE STRING "Select the Clock GPIO pin for second NES controller")
	set(NES_DATA_1 "12" CACHE STRING "Select the Data GPIO pin for second NES controller")
	set(NES_LAT_1 "11" CACHE STRING "Select the Latch GPIO pin for second NES controller")
    set(NES_PIO_1 "pio1" CACHE STRING "Select the PIO for second NES controller")
    set(WII_SDA "2" CACHE STRING "Select the SDA GPIO pin for Wii Classic controller")
    set(WII_SCL "3" CACHE STRING "Select the SCL GPIO pin for Wii Classic controller")
    set(WIIPAD_I2C "i2c1" CACHE STRING "Select the I2C bus for Wii Classic controller")
	set(UART_ENABLED 1 CACHE STRING "Enable UART output")
    set(USE_I2S_AUDIO 0 CACHE STRING "Enable I2S audio output") 
    set(PICO_AUDIO_I2S_DATA_PIN -1 CACHE STRING "Select the GPIO pin for I2S data")
    set(PICO_AUDIO_I2S_CLOCK_PIN_BASE -1 CACHE STRING "Select the GPIO pin for I2S clock")
    set(PICO_AUDIO_I2S_PIO 1 CACHE STRING "Select the PIO for I2S audio output")
    set(PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0 CACHE STRING "Set to 1 if the I2S clock pins are swapped")
    # NOTE! PIO USB does not work with Waveshare RP2040-PiZero when also using the hdmi. see https://www.waveshare.com/rp2040-pizero.htm
    set(PIO_USB_USE_PIO 2 CACHE BOOL "Select the PIO used for PIO-USB")
    set(PIO_DP_PLUS_PIN 6 CACHE STRING "PIO USB DP pin.")
elseif ( HW_CONFIG EQUAL 5 )
    # --------------------------------------------------------------------
	# Adafruit Metro RP2350
	# --------------------------------------------------------------------
	set(DVICONFIG "dviConfig_AdafruitMetroRP2350" CACHE STRING
    "Select a default pin configuration from common_dvi_pin_configs.h")
    set(LED_GPIO_PIN "23" CACHE STRING "Select the GPIO pin for LED")   # Adafruit fruitjam onboard LED
    set(SD_CS "39" CACHE STRING "Specify the Chip Select GPIO pin for the SD card")
    set(SD_SCK "34" CACHE STRING "Specify de Clock GPIO pin for the SD card")
    set(SD_MOSI "35" CACHE STRING "Select the Master Out Slave In GPIO pin for the SD card")
    set(SD_MISO "36" CACHE STRING "Select the Master In Slave Out GPIO pin for the SD card")
    set(SD_SPI "spi0" CACHE STRING "Select the SPI bus for SD card")
    set(NES_CLK "2" CACHE STRING "Select the Clock GPIO pin for NES controller")
    set(NES_DATA "3" CACHE STRING "Select the Data GPIO pin for NES controller")
    set(NES_LAT "4" CACHE STRING "Select the Latch GPIO pin for NES controller")
    set(NES_PIO "pio0" CACHE STRING "Select the PIO for NES controller")
    set(NES_CLK_1 "5" CACHE STRING "Select the Clock GPIO pin for second NES controller")
	set(NES_DATA_1 "6" CACHE STRING "Select the Data GPIO pin for second NES controller")
	set(NES_LAT_1 "7" CACHE STRING "Select the Latch GPIO pin for second NES controller")
    set(NES_PIO_1 "pio1" CACHE STRING "Select the PIO for second NES controller")
    set(WII_SDA "20" CACHE STRING "Select the SDA GPIO pin for Wii Classic controller")
    set(WII_SCL "21" CACHE STRING "Select the SCL GPIO pin for Wii Classic controller")
    set(WIIPAD_I2C "i2c0" CACHE STRING "Select the I2C bus for Wii Classic controller")
	set(UART_ENABLED 1 CACHE STRING "Enable UART output")
    set(USE_I2S_AUDIO 0 CACHE STRING "Enable external audio output") 
    set(PICO_AUDIO_I2S_DATA_PIN -1 CACHE STRING "Select the GPIO pin for I2S data")
    set(PICO_AUDIO_I2S_CLOCK_PIN_BASE -1 CACHE STRING "Select the GPIO pin for I2S clock")
    set(PICO_AUDIO_I2S_PIO 1 CACHE STRING "Select the PIO for I2S audio output")
    set(PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0 CACHE STRING "Set to 1 if the I2S clock pins are swapped")
    set(BOARD "adafruit_metro_rp2350")         # found in $PICO__SDK_PATH/lib/tinyusb/hw/bsp/rp2040/boards/adafruit_metro_rp2350/board.h
    set(PICO_BOARD "adafruit_metro_rp2350")    # found in $PICO__SDK_PATH/lib/tinyusb/hw/bsp/rp2040/boards/adafruit_metro_rp2350/adafruit_metro_rp2350.h
    set(PIO_USB_USE_PIO 2 CACHE BOOL "Select the PIO used for PIO-USB")
    set(PIO_DP_PLUS_PIN -1 CACHE STRING "PIO USB DP pin.")
    set(NEEDS_LATEST_TINYUSB_SDK 1 CACHE BOOL "Set to 1 if the latest TinyUSB SDK is needed for this board")
    
elseif ( HW_CONFIG EQUAL 6 )
    # --------------------------------------------------------------------
	# RP2040/RP2350 Tiny With PCB
	# --------------------------------------------------------------------
	set(DVICONFIG "dviConfig_RP2XX0_TinyPCB" CACHE STRING
    "Select a default pin configuration from common_dvi_pin_configs.h")
    set(LED_GPIO_PIN "-1" CACHE STRING "Select the GPIO pin for LED")  
    set(SD_CS "5" CACHE STRING "Specify the Chip Select GPIO pin for the SD card")
    set(SD_SCK "2" CACHE STRING "Specify de Clock GPIO pin for the SD card")
    set(SD_MOSI "3" CACHE STRING "Select the Master Out Slave In GPIO pin for the SD card")
    set(SD_MISO "4" CACHE STRING "Select the Master In Slave Out GPIO pin for the SD card")
    set(SD_SPI "spi0" CACHE STRING "Select the SPI bus for SD card")
    set(NES_CLK "14" CACHE STRING "Select the Clock GPIO pin for NES controller")
    set(NES_DATA "26" CACHE STRING "Select the Data GPIO pin for NES controller")
    set(NES_LAT "15" CACHE STRING "Select the Latch GPIO pin for NES controller")
    set(NES_PIO "pio0" CACHE STRING "Select the PIO for NES controller")
    set(NES_CLK_1 "27" CACHE STRING "Select the Clock GPIO pin for second NES controller")
	set(NES_DATA_1 "29" CACHE STRING "Select the Data GPIO pin for second NES controller")
	set(NES_LAT_1 "28" CACHE STRING "Select the Latch GPIO pin for second NES controller")
    set(NES_PIO_1 "pio1" CACHE STRING "Select the PIO for second NES controller")
    set(WII_SDA "-1" CACHE STRING "Select the SDA GPIO pin for Wii Classic controller")
    set(WII_SCL "-1" CACHE STRING "Select the SCL GPIO pin for Wii Classic controller")
    set(WIIPAD_I2C "i2c0" CACHE STRING "Select the I2C bus for Wii Classic controller")
	set(UART_ENABLED 0 CACHE STRING "Enable UART output") 
    set(USE_I2S_AUDIO 0 CACHE STRING "Enable I2S audio output") 
    set(PICO_AUDIO_I2S_DATA_PIN -1 CACHE STRING "Select the GPIO pin for I2S data")
    set(PICO_AUDIO_I2S_CLOCK_PIN_BASE -1 CACHE STRING "Select the GPIO pin for I2S clock")
    set(PICO_AUDIO_I2S_PIO 1 CACHE STRING "Select the PIO for I2S audio output")
    set(PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0 CACHE STRING "Set to 1 if the I2S clock pins are swapped")
    set(PIO_USB_USE_PIO 2 CACHE BOOL "Select the PIO used for PIO-USB")
    set(PIO_DP_PLUS_PIN -1 CACHE STRING "PIO USB DP pin.")
endif ( )

if (NOT DEFINED ENABLE_PIO_USB ) 
    set(ENABLE_PIO_USB 0 CACHE BOOL "Enable PIO USB support")
endif()
# Latest TinyUSB SDK is needed for Adafruit Metro RP2350 or when PIO USB is enabled
if (ENABLE_PIO_USB EQUAL 1)
    set(NEEDS_LATEST_TINYUSB_SDK 1 CACHE BOOL "Set to 1 if the latest TinyUSB SDK is needed for this board")
endif()
# If the board is not Adafruit Metro RP2350 and PIO USB is not enabled, set NEEDS_LATEST_TINYUSB_SDK to 0
if (NOT DEFINED NEEDS_LATEST_TINYUSB_SDK)
    set(NEEDS_LATEST_TINYUSB_SDK 0 CACHE BOOL "Set to 1 if the latest TinyUSB SDK is needed for this board")
endif()

if (NEEDS_LATEST_TINYUSB_SDK EQUAL 1)
    # ensure the file ${PICO_SDK_PATH}/lib/tinyusb/hw/bsp/rp2040/boards/adafruit_metro_rp2350/adafruit_metro_rp2350.h exists
    # This to check whether the latest master branch of TinyUsb is used.
    # This is needed for supporting the Adafruit Metro RP2350 or when PIO USB is enabled.
    if (NOT EXISTS ${PICO_SDK_PATH}/lib/tinyusb/hw/bsp/rp2040/boards/adafruit_metro_rp2350/adafruit_metro_rp2350.h)
        message(FATAL_ERROR [[
Please pull the latest master branch of TinyUsb in ${PICO_SDK_PATH}/lib/tinyusb/
This is needed for supporting the Adafruit Metro RP2350 or when PIO USB is enabled.               
               ]]
               )
    endif()
endif()
# Default PSRAM chip select pin. (RP2350 only)
# The emulator will try to detect if PSRAM is present. If so it will use it for loading roms.
# otherwise it will use flash memory to load roms.
if ( NOT DEFINED PSRAM_CS_PIN )
    # Pimoroni Pico Plus 2
    # Adafruit Metro RP2350
    set(PSRAM_CS_PIN 47 CACHE STRING "Select the GPIO pin for PSRAM chip select")
endif()
# --------------------------------------------------------------------
message("Hardware configuration: ${HW_CONFIG}")
message("Pico SDK board type   : ${PICO_BOARD}")
message("Tinyusb board type    : ${BOARD}")
#message("Using board header dir: ${PICO_BOARD_HEADER_DIRS}")  

if ( ENABLE_PIO_USB EQUAL 1 )
    message("PIO USB enabled, using PIO USB driver.")
    if ( PIO_DP_PLUS_PIN EQUAL -1 )
        message("PIO USB DP+           : From board config -> ${BOARD}")
    else()
        message("PIO USB DP+           : ${PIO_DP_PLUS_PIN}")
    endif()
    message("PIO USB PIO           : pio${PIO_USB_USE_PIO}")
    message("PIO USB path          : ${PICO_PIO_USB_PATH}")
else()
    message("PIO USB is disabled, using default USB driver.")
endif()
message("HDMI board type       : ${DVICONFIG}")
message("SD card CS            : ${SD_CS}")
message("SD card SCK           : ${SD_SCK}")
message("SD card MOSI          : ${SD_MOSI}")
message("SD card MISO          : ${SD_MISO}")
message("SD card SPI           : ${SD_SPI}")
message("NES controller 0 CLK  : ${NES_CLK}")
message("NES controller 0 DATA : ${NES_DATA}")
message("NES controller 0 LAT  : ${NES_LAT}")
message("NES controller 0 PIO  : ${NES_PIO}")
message("NES controller 1 CLK  : ${NES_CLK_1}")
message("NES controller 1 DATA : ${NES_DATA_1}")
message("NES controller 1 LAT  : ${NES_LAT_1}")
message("NES controller 1 PIO  : ${NES_PIO_1}")
message("Wii controller SDA    : ${WII_SDA}")
message("Wii controller SCL    : ${WII_SCL}")
message("Wii controller I2C    : ${WIIPAD_I2C}")
message("LED pin               : ${LED_GPIO_PIN}")
message("UART output enabled   : ${UART_ENABLED}")
message("I2S Audio enabled     : ${USE_I2S_AUDIO}")
message("I2S audio data pin    : ${PICO_AUDIO_I2S_DATA_PIN}")
message("I2S audio clock pin   : ${PICO_AUDIO_I2S_CLOCK_PIN_BASE}")
message("I2S audio PIO         : ${PICO_AUDIO_I2S_PIO}")
message("I2S audio clock swap  : ${PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED}")
message("PSRAM chip select pin : ${PSRAM_CS_PIN}")

