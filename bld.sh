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
	echo "Usage: $0 [-d] [-2 | -r] [-w] [-u] [-m] [-t path to toolchain] [ -p nprocessors] [-c <hwconfig>]"
	echo "Options:"
	echo "  -d: build in DEBUG configuration"
	echo "  -2: build for Pico 2 board (RP2350)"
	echo "  -r: build for Pico 2 board (RP2350) with riscv core"
	echo "  -u: enable PIO USB support (default is disabled)"
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
	echo "     5: Adafruit Metro RP2350"
	echo "     6: Waveshare RP2040-Zero/RP2350-Zero with custom PCB"
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
SDKVERSION=`cat $PICO_SDK_PATH/pico_sdk_version.cmake | grep "set(PICO_SDK_VERSION_MAJOR" | cut -f2  -d" " | cut -f1 -d\)`
TOOLCHAIN_PATH=
picoarmIsSet=0
picoRiscIsSet=0
USEPICOW=0
USEPIOUSB=0
CMAKEONLY=0
while getopts "muwhd2rc:t:p:" opt; do
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
	# HWCONFIG 5 is not compatible with pico
	if [[ $HWCONFIG -eq 5 ]] ; then
		echo "HWCONFIG $HWCONFIG is not compatible with Pico"
		exit 1
	fi
fi

# -w is not compatible with HWCONFIG 3, 4 and 5 those boards have no Wifi module
if [[ $USEPICOW -eq 1 && $HWCONFIG -gt 2 ]] ; then
	echo "Option -w is not compatible with HWCONFIG 3, 4, 5 and 6. Those boards have no Wifi module"
	exit 1
fi



case $HWCONFIG in
	1)
		UF2="${APP}PimoroniDV.uf2"
		;;
	2)
		UF2="${APP}AdaFruitDVISD.uf2"
		;;
	3) 
		UF2="${APP}FeatherDVI.uf2"
		;;
	4)
		UF2="${APP}WsRP2040PiZero.uf2"
		;;
	5)
		UF2="${APP}AdafruitMetroRP2350.uf2"
		;;
	6)
		UF2="${APP}RP2XX0ZeroWithPCB.uf2"
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
if [ "$PICO_PLATFORM" = "rp2350-riscv" ] ; then
	UF2="${PICO_BOARD}_riscv_$UF2"
else
	UF2="${PICO_BOARD}_$UF2"
fi
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
	cmake -DCMAKE_BUILD_TYPE=$BUILD -DPICO_BOARD=$PICO_BOARD -DHW_CONFIG=$HWCONFIG -DPICO_PLATFORM=$PICO_PLATFORM -DENABLE_PIO_USB=$USEPIOUSB .. || exit 1
else
	cmake -DCMAKE_BUILD_TYPE=$BUILD -DPICO_BOARD=$PICO_BOARD -DHW_CONFIG=$HWCONFIG -DPICO_PLATFORM=$PICO_PLATFORM -DENABLE_PIO_USB=$USEPIOUSB -DPICO_TOOLCHAIN_PATH=$TOOLCHAIN_PATH  .. ||  exit 1
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

