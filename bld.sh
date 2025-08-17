:
# Generic build script for the project
#
cd `dirname $0`/.. || exit 1
# APP=piconesPlus
# PROJECT="pico-InfoNESPlus"
[ -f CMakeLists.txt ] || { echo "CMakeLists.txt not found"; exit 1; }
# find string set(projectname in CMakeLists.txt
PROJECT=`grep -m 1 "set(projectname" CMakeLists.txt | cut -f2 -d"(" | cut -f2 -d" " | cut -f1 -d")"`
# exit if the project name is not found
[ -z "$PROJECT" ] && { echo "Project name not found"; exit 1; }	
APP=${PROJECT}
function usage() {
	echo "Build script for the ${PROJECT} project"
	echo  ""
	echo "Usage: $0 [-d] [-2 | -r] [-w] [-u] [-m] [-s ps-ram-cs] [-t path to toolchain] [ -p nprocessors] [-c <hwconfig>]"
	echo "Options:"
	echo "  -d: build in DEBUG configuration"
	echo "  -2: build for Pico 2 board (RP2350)"
	echo "  -r: build for Pico 2 board (RP2350) with riscv core"
	echo "  -u: enable PIO USB support (RP2350 only) disabled by default except for Waveshare RP2350-PiZero and Adafruit Fruit Jam."
	echo "  -s <ps-ram-cs>: specify the GPIO pin for PSRAM chip select (default is 47 for RP2350 boards)"
	echo "  -w: build for Pico_w or Pico2_w"
	echo "  -t <path to riscv toolchain>: only needed for riscv, specify the path to the riscv toolchain bin folder"
	echo "     Default is \$PICO_SDK_PATH/toolchain/RISCV_RPI_2_0_0_2/bin"
	echo "  -p <nprocessors>: specify the number of processors to use for the build"
	echo "  -c <hwconfig>: specify the hardware configuration"
	echo "     1: Pimoroni Pico DV Demo Base (Default)"
	echo "     2: Breadboard with Adafruit AdaFruit DVI Breakout Board and AdaFruit MicroSD card breakout board"
	echo "        Custom pcb"
	echo "     3: Adafruit Feather RP2040 DVI"
	echo "     4: Waveshare RP2040-PiZero"
	echo "     5: Adafruit Metro RP2350 (latest branch of TinyUSB is required for this board)"
	echo "     6: Waveshare RP2040-Zero/RP2350-Zero with custom PCB"
	echo "     7: WaveShare RP2350-PiZero - PIO USB enabled,  -u implied."
	echo "     8: Adafruit Fruit Jam - PIO USB enabled, -u implied."
	echo "  -m: Run cmake only, do not build the project"
	echo "  -h: display this help"
	echo ""
	echo "To install the RISC-V toolchain:"
	echo " - Raspberry Pi: https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.0.0-5/riscv-toolchain-14-aarch64-lin.tar.gz"
	echo " - X86/64 Linux: https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.0.0-5/riscv-toolchain-14-x86_64-lin.tar.gz"
	echo "and extract it to \$PICO_SDK_PATH/toolchain/RISCV_RPI_2_0_0_2"	
	echo ""
	echo "Example riscv toolchain install for Raspberry Pi OS:"
	echo ""
	echo -e "\tcd"
	echo -e "\tsudo apt-get install wget"
	echo -e "\twget https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.0.0-5/riscv-toolchain-14-aarch64-lin.tar.gz"
	echo -e "\tmkdir -p \$PICO_SDK_PATH/toolchain/RISCV_RPI_2_0_0_2"
	echo -e "\ttar -xzvf riscv-toolchain-14-aarch64-lin.tar.gz -C \$PICO_SDK_PATH/toolchain/RISCV_RPI_2_0_0_2"
	echo ""
	echo "To build for riscv:"
	echo ""
	echo -e "\t./bld.sh -c <hwconfig> -r -t \$PICO_SDK_PATH/toolchain/RISCV_RPI_2_0_0_2/bin"
	echo ""
} 
NPROC=$(nproc)
BUILDPROC=$NPROC
PICO_BOARD=pico
PICO_PLATFORM=rp2040
BUILD=RELEASE
HWCONFIG=1
UF2="${APP}PimoroniDV.uf2"
# check if var PICO_SDK is set and points to the SDK
if [ -z "$PICO_SDK_PATH" ] ; then
	echo "PICO_SDK_PATH not set. Please set the PICO_SDK_PATH environment variable to the location of the Pico SDK"
	exit 1
fi
# check if the SDK is present
if [ ! -d "$PICO_SDK_PATH" ] ; then
	echo "Pico SDK not found. Please set the PICO_SDK_PATH environment variable to the location of the Pico SDK"
	exit 1
fi
SDKVERSION=`cat $PICO_SDK_PATH/pico_sdk_version.cmake | grep "set(PICO_SDK_VERSION_MAJOR" | cut -f2  -d\( | cut -f2 -d" " | cut -f1 -d\)`
TOOLCHAIN_PATH=
picoarmIsSet=0
picoRiscIsSet=0
USEPICOW=0
USEPIOUSB=0
CMAKEONLY=0
PSRAM_CS_PIN=47 # default for RP2350 boards
USESIMPLEFILENAMES=0
while getopts "muwhd2rc:t:p:s:" opt; do
  case $opt in
    p)
	  BUILDPROC=$OPTARG
	  if [[ $BUILDPROC -lt 1 || $BUILDPROC -gt $NPROC ]] ; then
		  echo "Invalid value for -p, must be between 1 and $NPROC"
		  exit 1
	  fi
	  echo "Using $BUILDPROC processors for the build"
	  ;;
    d)
      BUILD=DEBUG
      ;;
    c)
      HWCONFIG=$OPTARG
	  # imply pico2 for HWCONFIG 5, 7 and 8
	  if [[ $HWCONFIG -eq 5 || $HWCONFIG -eq 7 || $HWCONFIG -eq 8 ]] ; then
		  PICO_BOARD=pico2
		  PICO_PLATFORM=rp2350-arm-s
		  picoarmIsSet=0    # not set via command line argument
		  echo "Using Pico 2 for HWCONFIG $HWCONFIG"
		  USESIMPLEFILENAMES=1
	  fi
	  # imply PIO USB for 7 and 8
	  if [[ $HWCONFIG -eq 7 || $HWCONFIG -eq 8 ]] ; then
		  USEPIOUSB=1
		  echo "Using PIO USB for HWCONFIG $HWCONFIG"
	  fi
      ;;
	2) 
	  PICO_BOARD=pico2
	  PICO_PLATFORM=rp2350-arm-s
	  picoarmIsSet=1
	  ;;
	r)
	  PICO_BOARD=pico2
	  picoriscIsSet=1
	  PICO_PLATFORM=rp2350-riscv
	  ;;	
	t) TOOLCHAIN_PATH=$OPTARG
	  ;;
	u) USEPIOUSB=1
	  ;;
	m) CMAKEONLY=1
	  ;;
	h)
	  usage
	  exit 0
	  ;;
	s)
	  PSRAM_CS_PIN=$OPTARG
	  ;;
	w) USEPICOW=1 
	  ;;
    \?)
      #echo "Invalid option: -$OPTARG" >&2
	  usage
      exit 1
      ;;
    :)
      echo "Option -$OPTARG requires an argument." >&2
	  usage
      exit 1
      ;;
	*)
	  usage
	  exit 1
	  ;;
  esac
done


# check toolchain if -r is set
if [[ $picoriscIsSet -eq 1 && -z "$TOOLCHAIN_PATH" ]] ; then
	# use default path
	TOOLCHAIN_PATH=$PICO_SDK_PATH/toolchain/RISCV_RPI_2_0_0_2/bin
fi

if [[ $picoriscIsSet -eq 0 && ! -z "$TOOLCHAIN_PATH" ]] ; then
	# -t is only valid  when using the riscv toolchain
	echo "Option -t is only valid with -r"
	exit 1
fi
# When PICOTOOLCHAIN_PATH is not empty it must containt the riscv toolchan
if [ ! -z "$TOOLCHAIN_PATH" ] ; then
	if [ ! -x "$TOOLCHAIN_PATH/riscv32-unknown-elf-gcc" ] ; then
		echo "riscv toolchain not found in $TOOLCHAIN_PATH"
		exit 1
	fi
fi

# -2 and -r are mutually exclusive
if [[ $picoarmIsSet -eq 1 && $picoriscIsSet -eq 1 ]] ; then
	echo "Options -2 and -r are mutually exclusive"
	exit 1
fi	

# if PICO_PLATFORM starts with rp2350, check if the SDK version is 2 or higher
if [[ $SDKVERSION -lt 2 && $PICO_PLATFORM == rp2350* ]] ; then
		echo "Pico SDK version $SDKVERSION does not support RP2350 (pico2). Please update the SDK to version 2 or higher"
		echo ""
		exit 1
fi
if [[ $PICO_PLATFORM == rp2350* ]] ; then
	# HWCONFIG 3 and 4 are not compatible with Pico 2
	if [[ $HWCONFIG -eq 3 || $HWCONFIG -eq 4 ]] ; then
		echo "HWCONFIG $HWCONFIG is not compatible with Pico 2"
		echo "Please use -c 1 or -c 2 or -c 5"
		exit 1
	fi
else
	# HWCONFIG 5, 7 and 8 is not compatible with pico
	if [[ $HWCONFIG -eq 5 || $HWCONFIG -eq 7 || $HWCONFIG -eq 8 ]] ; then
		echo "HWCONFIG $HWCONFIG is not compatible with Pico"
		exit 1
	fi
fi

# -w is not compatible with HWCONFIG 3, 4 and 5 those boards have no Wifi module
if [[ $USEPICOW -eq 1 && $HWCONFIG -gt 2 ]] ; then
	echo "Option -w is not compatible with HWCONFIG 3, 4, 5 and 6. Those boards have no Wifi module"
	exit 1
fi

if [[ $HWCONFIG -eq 5 || $USEPIOUSB -eq 1 ]] ; then
	usbvalid=1
	if [ ! -d "${PICO_SDK_PATH}/lib/tinyusb/hw/bsp/rp2040/boards/adafruit_metro_rp2350" ] ; then
		echo "You have not the latest master branch of the TinyUSB library."
		if [[ $HWCONFIG -eq 5 ]] ; then 
			 echo "This is needed for the Adafruit Metro RP2350."
		fi
		if [[ $USEPIOUSB -eq 1 ]] ; then
			echo "This is needed for PIO USB support."
		fi	
		echo "Please install the latest TinyUSB library:"
		echo " cd $PICO_SDK_PATH/lib/tinyusb"
		echo " git checkout master"
		echo " git pull"
		usbvalid=0	
	fi
	piovalid=1
	if [[ $USEPIOUSB -eq 1 ]] ; then
		# check if environment var PICO_PIO_USB_PATH is set and points to a valid path	
		if [ -z "$PICO_PIO_USB_PATH" ] ; then
			echo "PICO_PIO_USB_PATH not set."
			echo "Please set the PICO_PIO_USB_PATH environment variable to the location of the PIO USB library"
			piovalid=0
		elif [ ! -r "${PICO_PIO_USB_PATH}/src/pio_usb.h" ] ; then
			echo "No valid PIO USB repo found."
			echo "Please set the PICO_PIO_USB_PATH environment variable to the location of the PIO USB library"
			piovalid=0
		fi
		if [ $piovalid -eq 0 ] ; then
			echo "To install the PIO USB library:"
			echo " git clone https://github.com/sekigon-gonnoc/Pico-PIO-USB.git"
			echo " and set the PICO_PIO_USB_PATH environment variable to the location of the Pico-PIO-USB repository"
			echo " Example: export PICO_PIO_USB_PATH=~/Pico-PIO-USB"
			echo " or add it to your .bashrc file"
		fi
	fi
	if [[ $piovalid -eq 0 || $usbvalid -eq 0 ]] ; then
		echo "Please fix the above errors and try again."
		exit 1
	fi
fi
# use -u option only for RP2350 boards
if [[ $USEPIOUSB -eq 1 && $PICO_PLATFORM != rp2350* ]] ; then
	echo "Option -u (enable PIO-Usb) is only valid for RP2350 boards"
	exit 1
fi
case $HWCONFIG in
	1)
		UF2="PimoroniDVI"
		;;
	2)
		UF2="AdafruitDVISD"
		;;
	3) 
		UF2="FeatherDVI"
		USESIMPLEFILENAMES=1
		;;
	4)
		UF2="WaveShareRP2040PiZero"
		USESIMPLEFILENAMES=1
		;;
	5)
		UF2="AdafruitMetroRP2350"
		USESIMPLEFILENAMES=1
		;;
	6)
		UF2="WaveShareRP2XX0ZeroWithPCB"
		;;
	7)
		UF2="WaveShareRP2350PiZero"
		USESIMPLEFILENAMES=1
		;;
	8)
		UF2="AdafruitFruitJam"
		USESIMPLEFILENAMES=1
		;;
	*)
		echo "Invalid value: $HWCONFIG specified for option -c, must be 1, 2, 3 or 4"
		exit 1
		;;
esac

# add _w to PICO_BOARD if -w is set
if [ $USEPICOW -eq 1 ] ; then
	PICO_BOARD="${PICO_BOARD}_w"
fi
# if [ "$PICO_PLATFORM" = "rp2350-arm-s" ] ; then
# 	UF2="pico2_$UF2"
# fi	
PIOUSB=
if [ $USEPIOUSB -eq 1 ] ; then
	PIOUSB="_piousb"
fi	
# Only when SIMPLEFILENAMES=0
if [ $USESIMPLEFILENAMES -eq 0 ] ; then
	if [ "$PICO_PLATFORM" = "rp2350-riscv" ] ; then
		UF2="${UF2}_${PICO_BOARD}_riscv${PIOUSB}"
	else
		UF2="${UF2}_${PICO_BOARD}_arm${PIOUSB}"
	fi
else
	if [ "$PICO_PLATFORM" = "rp2350-riscv" ] ; then
		UF2="${UF2}_riscv${PIOUSB}"
	else
		UF2="${UF2}_arm${PIOUSB}"
	fi
fi
UF2="${APP}_${UF2}.uf2"
echo "Building $PROJECT"
echo "Using Pico SDK version: $SDKVERSION"
echo "Building for $PICO_BOARD, platform $PICO_PLATFORM with $BUILD configuration and HWCONFIG=$HWCONFIG"
[ ! -z "$TOOLCHAIN_PATH" ]  && echo "Toolchain path: $TOOLCHAIN_PATH"
echo "UF2 file: $UF2"

[ -d releases ] || mkdir releases || exit 1
if [ -d build ] ; then
	rm -rf build || exit 1
fi
mkdir build || exit 1
cd build || exit 1
if [ -z "$TOOLCHAIN_PATH" ] ; then
	cmake -DCMAKE_BUILD_TYPE=$BUILD -DPICO_BOARD=$PICO_BOARD -DHW_CONFIG=$HWCONFIG -DPICO_PLATFORM=$PICO_PLATFORM -DENABLE_PIO_USB=$USEPIOUSB -DPSRAM_CS_PIN=$PSRAM_CS_PIN .. || exit 1
else
	cmake -DCMAKE_BUILD_TYPE=$BUILD -DPICO_BOARD=$PICO_BOARD -DHW_CONFIG=$HWCONFIG -DPICO_PLATFORM=$PICO_PLATFORM -DENABLE_PIO_USB=$USEPIOUSB -DPSRAM_CS_PIN=$PSRAM_CS_PIN -DPICO_TOOLCHAIN_PATH=$TOOLCHAIN_PATH  .. ||  exit 1
fi
if [ $CMAKEONLY -eq 1 ] ; then
	echo "CMake configuration done, exiting as requested."
	exit 0
fi
make -j $BUILDPROC || exit 1
cd ..
echo ""
if [ -f build/${APP}.uf2 ] ; then
	cp build/${APP}.uf2 releases/${UF2} || exit 1
	picotool info releases/${UF2}
fi

