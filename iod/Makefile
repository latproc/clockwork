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
# Default release builds/installs iod-elc plus the support clients that share
# cw_client (ZMQ channel stack). Fast iod-only iterate:
#   make release-install RELEASE_TARGETS=iod-elc
# Prefer /usr/local (API 0.18+ install) then /opt/elc.
CMAKE_ELC_FLAGS ?= -DBUILD_IOD_ELC=ON -DBUILD_LEGACY_IOD=OFF -DCMAKE_PREFIX_PATH=/usr/local\;/opt/elc
export PKG_CONFIG_PATH := /usr/local/lib/pkgconfig:/opt/elc/lib/pkgconfig:$(PKG_CONFIG_PATH)
# Matches CMake install(TARGETS ...) for non-legacy binaries (not iod/iod_sdo).
RELEASE_TARGETS ?= iod-elc cw dbd iosh iocmd device_connector modbusd persistd

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
		targets=""; \
		for t in $(RELEASE_TARGETS); do \
			if [ -f "build/Release/CMakeFiles/$$t.dir/build.make" ]; then \
				targets="$$targets $$t"; \
			else \
				echo "skipping $$t (not configured)"; \
			fi; \
		done; \
		$(MAKE) -C build/Release $(JOBS) $$targets; \
	fi

# Install Release binaries into iod/ (project root). Safe while iod is running:
# write to a temp name then rename over the live path (avoids ETXTBSY on open
# inode). Running process keeps the old image until svc -k / restart.
# RELEASE_TARGETS=all uses CMake install (same destination). Named targets are
# copied one-by-one so a subset rebuild still lands next to the live tree.
release-install: release
	@if [ "$(RELEASE_TARGETS)" = "all" ]; then \
		$(MAKE) -C build/Release $(JOBS) install; \
	else \
		echo "Installing $(RELEASE_TARGETS) -> $(CURDIR)/"; \
		for t in $(RELEASE_TARGETS); do \
			src="build/Release/$$t"; \
			if [ ! -f "build/Release/CMakeFiles/$$t.dir/build.make" ]; then \
				echo "skipping $$t (not configured)"; \
				continue; \
			fi; \
			test -x "$$src" || { echo "missing $$src"; exit 1; }; \
			tmp="./.$$t.new.$$$$"; \
			cp -a "$$src" "$$tmp" && mv -f "$$tmp" "./$$t"; \
			ls -la "./$$t"; \
			case "$$t" in \
				iod-elc|iod|iod_sdo|cw|cw_ecat|dbd|persistd|modbusd|device_connector) \
					if [ -d "/etc/service/$$t" ] || [ -d "/service/$$t" ]; then \
						echo "  live process keeps old image until: svc -k /etc/service/$$t"; \
					else \
						echo "  daemon: restart if already running (atomic replace; no ETXTBSY)"; \
					fi ;; \
				*) ;; \
			esac; \
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
