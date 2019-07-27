JOBS = # -j 2
all:	
	[ -d "build" ] || mkdir build
	cd build && cmake .. && make $(JOBS) install

test:
	cd build && make $(JOBS) test

release:
	[ -d "build" ] || mkdir build
	[ -d "build/Release" ] || mkdir build/Release
	cd build/Release && cmake -DCMAKE_BUILD_TYPE=Release ../.. && make $(JOBS)

release-install:
	[ -d "build" ] || mkdir build
	[ -d "build/Release" ] || mkdir build/Release
	cd build/Release && cmake -DCMAKE_BUILD_TYPE=Release ../.. && make $(JOBS) install

release-test:
	cd build/Release && make test

debug:
	[ -d "build" ] || mkdir build
	[ -d "build/Debug" ] || mkdir build/Debug
	cd build/Debug && cmake -DCMAKE_BUILD_TYPE=Debug ../.. && make $(JOBS)

debug-test:
	cd build/Debug && make test

debug-install:	debug
	cd build/Debug && make install

xcode:
	[ -d "xcode" ] || mkdir xcode
	cd xcode && cmake -G Xcode .. && xcodebuild -parallelizeTargets -jobs 6


clean:
	rm -rf build
