#
# make
# make all   -- build everything
#
# make test  -- build all unit tests, do not run
#
# make run   -- run all unit tests (including the textbook chapter tests)
#
# make install -- install b6parse, b6lower, b6codegen, libc.bin, libbem.bin,
#                 libruntime.a and the compiler-owned headers (the C11 freestanding
#                 subset plus besm6.h); rv64codegen with the RISC-V runtime and
#                 headers -- to ~/.local if it exists, else /usr/local
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
	@prefix=$$( [ -d "$$HOME/.local" ] && echo "$$HOME/.local" || echo /usr/local ); \
	echo "Installing to $$prefix"; \
	cmake --install build --prefix "$$prefix"

clean:
	rm -rf build

build:
	mkdir $@
	cmake -B$@ -DCMAKE_BUILD_TYPE=RelWithDebInfo

debug:
	mkdir build
	cmake -Bbuild -DCMAKE_BUILD_TYPE=Debug
