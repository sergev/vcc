#
# make
# make all   -- build everything
#
# make test  -- build all unit tests, do not run
#
# make run   -- run all unit tests (including the textbook chapter tests)
#
# make install -- install vparse, vlower, vgenbesm6 with libc.bin, libbem.bin,
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
	$(MAKE) -Cbuild all

run:    test
	ctest --test-dir build --progress

install: all
	@echo "Installing to $$HOME/.local"; \
	cmake --install build --prefix "$$HOME/.local"

clean:
	rm -rf build

build:
	mkdir $@
	cmake -B$@ -DCMAKE_BUILD_TYPE=RelWithDebInfo

debug:
	mkdir build
	cmake -Bbuild -DCMAKE_BUILD_TYPE=Debug
