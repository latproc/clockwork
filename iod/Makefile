ASTYLE := $(shell command -v astyle 2>/dev/null)
CLANGFORMAT := $(shell command -v clang-format 2>/dev/null)
ifndef JOBS
    JOBS:=-j6
endif

all:
	[ -d "build" ] || mkdir build
	cd build && cmake .. && make $(JOBS) install


release:
	[ -d "build" ] || mkdir build
	[ -d "build/Release" ] || mkdir build/Release
	cd build/Release && cmake -DCMAKE_BUILD_TYPE=Release ../.. && make -j 3

release-install:
	[ -d "build" ] || mkdir build
	[ -d "build/Release" ] || mkdir build/Release
	cd build/Release && cmake -DCMAKE_BUILD_TYPE=Release ../.. && make -j 5 install

debug:
	[ -d "build" ] || mkdir build
	[ -d "build/Debug" ] || mkdir build/Debug
	cd build/Debug && cmake -DCMAKE_BUILD_TYPE=Debug ../.. && make $(JOBS)

debug-install:	debug
	cd build/Debug && make install

xcode:
	[ -d "xcode" ] || mkdir xcode
	cd xcode && cmake -G Xcode .. && xcodebuild -parallelizeTargets $(JOBS)

test:
	[ -d ".test" ] || mkdir .test
	cd .test && cmake -DCMAKE_BUILD_TYPE=Debug -DRUN_TESTS=ON .. && make $(JOBS) && make test

style:
ifdef ASTYLE
	astyle --project src/*.c,*.cpp,*.h,*.hpp tests/*.h,*.cpp
endif
ifdef CLANGFORMAT
	clang-format --style=file -i `find src -name \*.c -o -name \*.cpp -o -name \*.h -o -name \*.hpp`
	clang-format --style=file -i `find tests -name \*.c -o -name \*.cpp -o -name \*.h -o -name \*.hpp`
endif
