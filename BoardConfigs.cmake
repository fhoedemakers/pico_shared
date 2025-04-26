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
endif ( )

# --------------------------------------------------------------------
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