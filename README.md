# Jou programming language

Jou is an experimental toy programming language.

Goals:
- Minimalistic feel of C + simple Python-style syntax
- Possible target audiences:
    - People who find C programming fun
    - Python programmers who want to try programming at a lower level
- Compatibility with C, not just as one more feature but as the recommended way to use libraries
- Self-hosted compiler
- Eliminate some stupid things in C. For example:
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
- Error messages for most common bugs in C (missing `free()`, double `free()`, use after free, etc.)
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
    (e.g. complicated import system with weird syntax and runtime behavior)

Here's what Jou code currently looks like:

```python3
cdecl putchar(ch: int) -> int

def main() -> int:
    putchar('h')
    putchar('e')
    putchar('l')
    putchar('l')
    putchar('o')
    putchar('\n')
    return 0
```

Here's what I want it to eventually look like:

```python3
import io

def main() -> int:
    printf("hello\n")
    return 0
```

And if you look inside `io.jou`, here's what I want it to look like:

```python3
link libc.so

...

cdecl putchar(ch: int) -> int

...
```


## Setup

You need:
- An operating system that is something else than Windows
- Git
- LLVM 11
- clang 11
- make

If you are on a Debian-based linux distro, you can install everything you need like this:

```
$ sudo apt install git make llvm-11-dev clang-11
```

Once you have installed the dependencies,
run these commands to compile the Jou compiler and then run a hello world program:

```
$ make -j2
$ ./jou examples/hello.jou
hello
```

TODO: add `git clone` command to beginning


## Developing the compiler

The compiler is currently written in C.
To get a good idea of how it works,
you can look at what it produces in each compilation step:

```
$ ./jou --verbose examples/hello.jou
```

This shows the tokens, AST and LLVM IR generated. So at a high level, the compilation steps are:
- Tokenize: split the source code into tokens
- Parse: build an Abstract Syntax Tree from the tokens
- Codegen: convert the AST into LLVM IR
- Invoke `clang` and pass it the generated LLVM IR

Running tests:

```
$ make test
```

This runs Jou files in `examples/` and `tests/`,
and ensures that they output what is expected.
The expected output is auto-generated from `# Output:` and `# Error:` comments in the Jou files:

- A comment like `# Output: foo` appends a line `foo` to the expected output.
- A comment like `# Error: foo` on line 123 of file `tests/bar.jou` appends a line
    `compile error in file "tests/bar.jou", line 123: foo`.
- Files in `tests/should_fail/` should cause a compiler error (exit code 1).
    Other files should run successfully (exit code 0).

If the actual output doesn't match what's in a text file, you will see a diff where
green (+) is the program's output and red (-) is what was expected.
The command that was ran (e.g. `./jou examples/hello.jou`) is shown just above the diff,
and you can run it again manually to debug a test failure.
You can also put e.g. `valgrind` or `gdb --args` in front of the command.

Files in `tests/broken/` are not tested,
because for one reason or another they don't work as intended.
Ideally the `tests/broken/` directory would be empty most of the time.

Checking for memory related bugs with valgrind:

```
$ make -j2 && valgrind --leak-check=full --show-leak-kinds=all ./jou examples/hello.jou
```

TODO:
- Figure out a reasonable way to use valgrind. Seems like llvm does something messy?
- Write syntax spec once syntax seems relatively stable
- Multiple types. Currently everything is `int` which is 32-bit signed int.
- Some way to include types in error messages:
    - A second pass that adds type information to AST nodes (and checks the types)? New nodes, or filling new fields in existing nodes?
    - Map LLVM types back to jou-programmer-readable strings, similar to `AstType.name`? Feels like a hack that I would need to change later.
- A boolean datatype that presumably doesn't cast between ints or pointers too easily.
- `if`,`elif`,`else`
- `while`, `for init; cond; incr:`, maybe `do`-`while`?
- `++` and `--`
    - There's now quite a lot of overlap between expressions and statements.
        Do something about it?
- Strings:
    - a `byte` type (similar to `unsigned char` in c)
    - string literals (do i want `const` pointers? if so: which syntax, `foo: int const*` or `foo: const int*`?)
    - const pointers so u can't modify string literals
        - I really want to use a keyword for this, so make it `const` as in C, but where to put it?
            - `foo: const int*`: looks natural to me, but would complicate stuff because the related `const` and `*` are far apart
            - `foo: int const*` looks weird to me, but makes much more sense
            - `foo: int* const` would be very confusing when you also program in C...
        - do I even want const pointers, or just tell users to be careful?
- Everything else...?
    - Structs
    - Enums
    - A reasonable way to import structs from C (not just functions).
        - I don't like the zig/rust things that attempt to parse header files and get confused by macros. Something else?
        - A good combination of odd corner cases for testing is probably `struct stat` and the `stat()` function.
    - Self-hosted compiler??!?!
