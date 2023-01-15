# Jou programming language

Jou is an experimental toy programming language. It looks like this:

```python3
declare puts(string: byte*) -> int

def main() -> int:
    puts("Hello World")
    return 0
```

See the [examples](./examples/) and [tests](./tests/) directories for more example programs.

Goals:
- Minimalistic feel of C + simple Python-style syntax
- Possible target audiences:
    - People who find C programming fun
    - Python programmers who want to try programming at a lower level (maybe to eventually learn C or Rust)
- Compatibility with C, not just as one more feature but as the recommended way to do many things
- Self-hosted compiler
- Eliminate some stupid things in C. For example:
    - Manu useful warnings being disabled by default
    - UB for comparing pointers into different memory areas
        (as in `array <= foo && foo < array+sizeof(array)/sizeof(array[0])`)
    - `negative % positive` is negative or zero, should IMO be positive or zero
        (unless that is a lot slower, of course)
    - Strict aliasing
    - `int` possibly being only 16 bits
    - `long` possibly being only 32 bits
    - `char` possibly being more than 8 bits
    - `char` possibly being signed
    - `char` being named `char` even though it's really a byte
- Generics, so that you can implement a generic `list` (dynamically growing array)
    better than in C
- Compiler errors for most common bugs in C (missing `free()`, double `free()`, use after free, etc.)
- More keywords (`def`, `decl`, `forwarddecl`)
- Enumerated unions = C `union` together with a C `enum` to tell which union member is active
- Windows support that doesn't suck

Non-goals:
- Yet another big language that doesn't feel at all like C (C++, Zig, Rust, ...)
- Garbage collection (should feel lower level than that)
- Wrapper functions for the C standard library
- Wrapper libraries for existing C libraries (should just use the C library directly)
- Trying to detect every possible memory bug at compile time
    (Rust already does it better than I can, and even then it can be painful to use)
- Copying Python's gotchas
    (e.g. complicated import system with weird syntax and much more weird runtime behavior)


## Setup

You need:
- An operating system that is something else than Windows
- Git
- LLVM 11
- clang 11
- make
- valgrind

If you are on a linux distro that has `apt`, you can install everything you need like this:

```
$ sudo apt install git llvm-11-dev clang-11 make valgrind
```

Once you have installed the dependencies,
run these commands to compile the Jou compiler and then run a hello world program:

```
$ git clone https://github.com/Akuli/jou
$ cd jou
$ make
$ ./jou examples/hello.jou
Hello World
```


## How does the compiler work?

The compiler is currently written in C. At a high level, the compilation steps are:
- Tokenize: split the source code into tokens
- Parse: build an Abstract Syntax Tree from the tokens
- Typecheck: errors for wrong number or type of function arguments etc, figure out the type of each expression in the AST
- Build CFG: build Control Flow Graphs for each function from the AST
- Simplify CFG: simplify and analyze the control flow graphs in various ways, emit warnings as needed
- Codegen: convert the CFGs into LLVM IR
- Invoke `clang` and pass it the generated LLVM IR

To get a good idea of how these steps work,
you can look at what the compiler produces in each compilation step:

```
$ ./jou --verbose examples/hello.jou
```

This shows the tokens, AST, CFGs and LLVM IR generated.
The control flow graphs are shown twice, before and after simplifying them.

After exploring the verbose output, you should probably
read `src/jou_compiler.h` and have a quick look at `src/util.h`.


## Tests

GitHub Actions runs all tests when you make a pull request,
so you don't need to run tests locally if you only intend to fix a couple small things.
That said, test-driven development works very well for developing compilers.
There should be a test (or a TODO comment about adding a test)
for every feature and for every compiler error/warning.

Running tests:

```
$ make test         # Quick run. Good for local development.
$ make valgrind     # Run some of the tests with valgrind.
$ make fulltest     # This is slow. Includes the other two. Runs in CI.
```

Each of these commands:
- compiles the Jou compiler if you have changed something in `src/` since the last time it was compiled
- runs all Jou files in `examples/` and `tests/` (`make valgrind` only runs some files, see below)
- ensures that the Jou files output what is expected.

The expected output is auto-generated from comments in the Jou files:

- A comment like `# Output: foo` appends a line `foo` to the expected output.
- A comment like `# Error: foo` on line 123 of file `tests/bar/baz.jou` appends a line
    `compiler error in file "tests/bar/baz.jou", line 123: foo`.
- A comment like `# Warning: foo` on line 123 of file `tests/bar/baz.jou` appends a line
    `compiler warning for file "tests/bar/baz.jou", line 123: foo`.
- Files in `examples/` and `tests/should_succeed/` should run successfully (exit code 0).
    All other files should cause a compiler error (exit code 1).

If the actual output doesn't match the expected output, you will see diffs where
green (+) is the program's output and red (-) is what was expected.
The command that was ran (e.g. `./jou examples/hello.jou`) is shown just above the diff,
and you can run it again manually to debug a test failure.
You can also put e.g. `valgrind` or `gdb --args` in front of the command.

The purpose of `make valgrind` is to find missing `free()`s and various other memory bugs.
It doesn't do anything with tests that are supposed to fail with an error, for a few reasons:
- The compiler does not free memory allocations when it exits with an error.
    This is fine because the operating system will free the memory anyway,
    but `valgrind` would see it as many memory leaks.
- Valgrinding is slow. Most tests are about compiler errors,
    and `make valgrind` would take several minutes if they weren't skipped.
- Most problems in error message code are spotted by non-valgrinded tests.

Sometimes the fuzzer discovers a bug that hasn't been caught with tests.
It mostly finds bugs in the tokenizer,
because the fuzzer works by feeding random bytes to the compiler.

```
$ ./fuzzer.sh
```


## LLVM versions

Jou supports LLVM 11 and LLVM 13.
The default is LLVM 11, because I believe it is more widely available.
To use LLVM 13 instead, set `LLVM_CONFIG` when compiling:

```
$ sudo apt install llvm-13-dev clang-13
$ make clean
$ LLVM_CONFIG=llvm-config-13 make
```

Other versions of LLVM may work too.
Please create an issue if you need to use a different version of LLVM.


## TODO

This list tends to have a few outdated details,
but it should give you some kind of idea about what is still missing.

- Write syntax spec once syntax seems relatively stable
- JIT so that you don't need to go through a compile step
- REPL, if possible?
- Arrays
- Structs
- Enums
- A reasonable way to import structs from C (not just functions).
    - I don't like the zig/rust things that attempt to parse header files and get confused by macros. Something else?
    - A good combination of odd corner cases for testing is probably `struct stat` and the `stat()` function.
- Self-hosted compiler??!?!
