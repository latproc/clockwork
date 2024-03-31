# setup and run cmake

all:	build build/CMakeFiles build/mbmon

build:
	mkdir build

build/CMakeFiles:	CMakeLists.txt build
	(cd build; MODBUS_DIR=.. cmake .. )

build/mbmon:	build/CMakeFiles src/*.h src/*.cpp
	(cd build; make )

test:
	[ -d ".test" ] || mkdir .test
	cd .test && cmake -DCMAKE_BUILD_TYPE=Debug -DRUN_TESTS=ON .. && make $(JOBS) &&  make test

style:
	clang-format --style=file -i `find src -name \*.c -o -name \*.cpp -o -name \*.h -o -name \*.hpp`

clean:
	rm -rf build
