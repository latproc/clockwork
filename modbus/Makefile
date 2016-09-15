# setup and run cmake

all:	build build/CMakeFiles build/mbmon

build:
	mkdir build

build/CMakeFiles:	CMakeLists.txt build
	(cd build; MODBUS_DIR=.. cmake .. )

build/mbmon:	build/CMakeFiles src/mbmon.cpp
	(cd build; make )

clean:
	rm -rf build
