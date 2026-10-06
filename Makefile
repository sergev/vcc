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

clean:
	rm -rf build

build:
	mkdir $@
	cmake -B$@ -DCMAKE_BUILD_TYPE=RelWithDebInfo

debug:
	mkdir build
	cmake -Bbuild -DCMAKE_BUILD_TYPE=Debug
