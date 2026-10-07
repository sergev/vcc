#
# make
# make all   -- build the compiler and the runtime libraries (no tests)
#
# make test  -- build the compiler and all unit tests, do not run
#
# make run   -- build and run all unit tests (including the textbook chapter tests)
#
# make install -- install vcc, vcpp, vparse, vlower, vgenbesm6 with libc.bin, libbem.bin,
#                 libruntime.a and the compiler-owned headers (the C11 freestanding
#                 subset plus besm6.h); vgenriscv64 with the RISC-V runtime and
#                 headers -- to ~/.local
#
# make self -- build vcc with vcc: stage 1 (./build/) is installed into ./build/stage/,
#               whose vcc compiles stage 2, the compiler and the runtime, in ./build/self/
#
# make self-test -- make self, then build and run the unit tests against stage 2
#
# make clean -- remove build files
#
# To reconfigure for Debug build:
#   make clean; make debug; make
#
all:    build
	$(MAKE) -Cbuild $@

test:   build
	$(MAKE) -Cbuild all tests

run:    test
	ctest --test-dir build --progress

install: all
	cmake --install build

STAGE = $(CURDIR)/build/stage

self:   all
	cmake --install build --prefix $(STAGE)
	cmake -Bbuild/self -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=$(STAGE)/bin/vcc
	$(MAKE) -Cbuild/self all

self-test: self
	$(MAKE) -Cbuild/self tests
	ctest --test-dir build/self --progress

clean:
	rm -rf build

build:
	mkdir $@
	cmake -B$@ -DCMAKE_BUILD_TYPE=RelWithDebInfo

debug:
	mkdir build
	cmake -Bbuild -DCMAKE_BUILD_TYPE=Debug
