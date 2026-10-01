# "Writing a C Compiler" run tests

The book's valid programs, shared by every backend. Each test is
`EXPECT_EQ(<stdout + main's return value>, CompileAndRunBook(<source>))` against a
`BookTest` fixture that the backend defines in its own `book_test.h`; the backend's
test executable compiles these sources with its own include path. A program a
target cannot run goes on that fixture's skip list.

The programs were imported while BESM-6 was the only target, so some are adapted
to it (`putch` for `putchar`, values that fit both 41 and 64 bits); the comments
record why. A program whose result depends on integer widths or sizes is kept
here in its generic LP64 form (32-bit `int`, 64-bit `long` and pointers, as in the
book), with `signed char` wherever the book assumed plain `char` is signed. BESM-6
skips those and runs its own versions from
`backend/besm6/test/book_besm6_tests.cpp`. The RISC-V fixture also compiles every
program with clang and checks that the output is the same.
