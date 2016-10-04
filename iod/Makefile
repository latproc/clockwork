all:	
	[ -d "build" ] || mkdir build
	cd build && cmake .. && make -j


release:
	[ -d "build" ] || mkdir build
	[ -d "build/Release" ] || mkdir build/Release
	cd build/Release && cmake -DCMAKE_BUILD_TYPE=Release ../.. && make -j 3

release-install:
	cd build/Release && make -j 6 install

debug:
	[ -d "build" ] || mkdir build
	[ -d "build/Debug" ] || mkdir build/Debug
	cd build/Debug && cmake -DCMAKE_BUILD_TYPE=Debug ../.. && make -j 6

debug-install:	debug
	cd build/Debug && make install
