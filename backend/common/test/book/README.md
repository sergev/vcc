# "Writing a C Compiler" run tests

The book's valid programs, shared by every backend. Each test is
`EXPECT_EQ(<stdout + main's return value>, CompileAndRunBook(<source>))` against a
`BookTest` fixture that the backend defines in its own `book_test.h`; the backend's
test executable compiles these sources with its own include path. A program a
target cannot run goes on that fixture's skip list.

The programs were imported while BESM-6 was the only target, so some are adapted
to it (`putch` for `putchar`, values within 41 bits, BESM-6 `sizeof` results in
chapter 17); the comments record why.
