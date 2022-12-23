# Jou programming language

Jou is an experimental toy programming language.

Goals:
- Minimalistic feel of C + simple Python-style syntax
- Possible target audiences:
    - People who find C programming fun
    - Python programmers who want to try programming at a lower level
- Compatibility with C, not just as one more feature but as the recommended way to use libraries
- Self-hosted compiler
- Eliminate some stupid things in C
    (UB for comparing pointers into different memory areas,
    UB for strict aliasing,
    `int` possibly being only 16 bits,
    `long` possibly being only 32 bits,
    `char` possibly being more than 8 bits,
    `char` possibly being signed,
    `char` being named `char` even though it's really a byte,
    etc)
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
cimport int putchar(int ch)

int main():
    putchar('h')
    putchar('e')
    putchar('l')
    putchar('l')
    putchar('o')
    putchar('\n')
    return '\0'
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
run these commands to compile the Jou compiler and then run a hello world progra,:

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
$ ./jou --verbose examples/hello.nl
```

This shows the tokens, AST and LLVM IR generated. So at a high level, the compilation steps are:
- Tokenize: split the source code into tokens
- Parse: build an Abstract Syntax Tree from the tokens
- Codegen: convert the AST into LLVM IR
- Invoke `clang` and pass it the generated LLVM IR

Checking for memory related bugs with valgrind:

```
$ make -j2 && valgrind --leak-check=full --show-leak-kinds=all ./jou examples/hello.nl
```

TODO:
- Figure out a reasonable way to use valgrind. Seems like llvm does something messy?
- tests!!!! Test all error messages there currently are.
- Rework the syntax.
- Write syntax spec once syntax seems relatively stable
- Multiple types. Currently everything is `int` which is 32-bit signed int.
- Everything else...?
