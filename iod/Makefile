# for parallel compile, use JOBS=-jN make ..
# where N is the number of parallel jobs to use

ASTYLE := $(shell command -v astyle 2>/dev/null)
CLANGFORMAT := $(shell command -v clang-format 2>/dev/null)

all:	
	[ -d "build" ] || mkdir build
	cd build && cmake .. && make $(JOBS) install

test:
	cd build/lib/clockwork_client.git && ctest
	cd build && ctest

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

style:
ifdef ASTYLE
	astyle --project --recursive lib/clockwork_interpreter/src/*.c,*.cpp,*.h,*.hpp
endif
ifdef CLANGFORMAT
	clang-format -i --verbose `find lib/clockwork_interpreter/src -name \*.c -o -name \*.cpp -o -name \*.h -o -name \*.hpp`
endif
