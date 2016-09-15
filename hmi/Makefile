# setup and run cmake

all:	build build/CMakeFiles build/modbus_sample

build:
	mkdir build

build/CMakeFiles:	CMakeLists.txt build
	(cd build; MODBUS_DIR=.. cmake .. )

build/modbus_sample:	build/CMakeFiles src/modbus_sample.cxx
	(cd build; make )

clean:
	rm -rf build
