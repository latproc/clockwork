ASTYLE := $(shell command -v astyle 2>/dev/null)
CLANGFORMAT := $(shell command -v clang-format 2>/dev/null)
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    ASAN_OPTIONS ?= strict_string_checks=1:abort_on_error=1:halt_on_error=1
else
    ASAN_OPTIONS ?= detect_leaks=1:strict_string_checks=1:abort_on_error=1:halt_on_error=1
endif
UBSAN_OPTIONS ?= print_stacktrace=1:halt_on_error=1
CTEST_TIMEOUT ?= 60

ifndef JOBS
    JOBS:=-j6
endif

# This branch: iod-elc (kernel elc) + cw + support tools. Legacy iod/iod_sdo off.
# Override: make release CMAKE_EXTRA='-DBUILD_LEGACY_IOD=ON'
# Default release builds only iod-elc (fast iterate). Full tree:
#   make release RELEASE_TARGETS=all
CMAKE_ELC_FLAGS ?= -DBUILD_IOD_ELC=ON -DBUILD_LEGACY_IOD=OFF -DCMAKE_PREFIX_PATH=/opt/elc
export PKG_CONFIG_PATH := /opt/elc/lib/pkgconfig:$(PKG_CONFIG_PATH)
RELEASE_TARGETS ?= iod-elc

all:
	[ -d "build" ] || mkdir build
	cd build && cmake -DCMAKE_BUILD_TYPE=Debug $(CMAKE_ELC_FLAGS) $(CMAKE_EXTRA) .. && make $(JOBS) && make install

release:
	[ -d "build" ] || mkdir build
	[ -d "build/Release" ] || mkdir build/Release
	cd build/Release && cmake -DCMAKE_BUILD_TYPE=Release $(CMAKE_ELC_FLAGS) $(CMAKE_EXTRA) ../..
	@if [ "$(RELEASE_TARGETS)" = "all" ]; then \
		$(MAKE) -C build/Release $(JOBS); \
	else \
		$(MAKE) -C build/Release $(JOBS) $(RELEASE_TARGETS); \
	fi

# Install Release binary into iod/ (CMake DESTINATION = project root).
# Stop iod first if you get "Text file busy".
release-install: release
	@if [ "$(RELEASE_TARGETS)" = "all" ]; then \
		$(MAKE) -C build/Release $(JOBS) install; \
	else \
		echo "Installing $(RELEASE_TARGETS) -> $(CURDIR)/"; \
		for t in $(RELEASE_TARGETS); do \
			src="build/Release/$$t"; \
			test -x "$$src" || { echo "missing $$src"; exit 1; }; \
			cp -a "$$src" "./$$t" || { echo "cp failed (iod running? svc -d /etc/service/iod)"; exit 1; }; \
			ls -la "./$$t"; \
		done; \
	fi

client:
	[ -d "build" ] || mkdir build
	[ -d "build/Release" ] || mkdir build/Release
	cd build/Release && cmake -DCMAKE_BUILD_TYPE=Release $(CMAKE_ELC_FLAGS) $(CMAKE_EXTRA) ../.. -DRUN_TESTS=OFF && cmake --build . --target cw_client -- $(JOBS)

client-install:	client
	cd build/Release && cmake --build . --target install_client -- $(JOBS)

debug:
	[ -d "build" ] || mkdir build
	[ -d "build/Debug" ] || mkdir build/Debug
	cd build/Debug && cmake -DCMAKE_BUILD_TYPE=Debug $(CMAKE_ELC_FLAGS) $(CMAKE_EXTRA) ../.. && make $(JOBS)

debug-install:	debug
	cd build/Debug && make install

xcode:
	[ -d "xcode" ] || mkdir xcode
	cd xcode && cmake -G Xcode .. && xcodebuild -parallelizeTargets

test:
	[ -d "build" ] || mkdir build
	cd build && cmake -DCMAKE_BUILD_TYPE=Debug $(CMAKE_ELC_FLAGS) $(CMAKE_EXTRA) -DRUN_TESTS=ON .. && make $(JOBS) install && make test

asan-test:
	[ -d "build" ] || mkdir build
	[ -d "build/Asan" ] || mkdir build/Asan
	cd build/Asan && cmake -DCMAKE_BUILD_TYPE=Debug -DRUN_TESTS=ON -DUSE_ASAN=ON ../.. && cmake --build . -- $(JOBS)
	cd build/Asan && ASAN_OPTIONS="$(ASAN_OPTIONS)" UBSAN_OPTIONS="$(UBSAN_OPTIONS)" ctest --output-on-failure --timeout $(CTEST_TIMEOUT)

style:
ifdef ASTYLE
	astyle --project src/*.c,*.cpp,*.h,*.hpp tests/*.h,*.cpp
endif
ifdef CLANGFORMAT
	clang-format --style=file -i `find src -name \*.c -o -name \*.cpp -o -name \*.h -o -name \*.hpp`
	clang-format --style=file -i `find tests -name \*.c -o -name \*.cpp -o -name \*.h -o -name \*.hpp`
endif

fix-style:
ifdef CLANGFORMAT
	./fix-format.sh
endif
