# Jou programming language

Jou is an experimental toy programming language. It looks like this:

```python3
cdecl puts(string: byte*) -> int

def main() -> int:
    puts("Hello World")
    return 0
```

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
    (e.g. complicated import system with weird syntax and much more weird runtime behavior)


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
At a high level, the compilation steps are:
- Tokenize: split the source code into tokens
- Parse: build an Abstract Syntax Tree from the tokens
- Fill types: find the types of all expressions and put them to AST
- Codegen: convert the AST into LLVM IR
- Invoke `clang` and pass it the generated LLVM IR

To get a good idea of how these steps work,
you can look at what the compiler produces in each compilation step:

```
$ ./jou --verbose examples/hello.jou
```

This shows the tokens, AST and LLVM IR generated.
The AST is shown twice, once before filling the types (most types are `?`)
and again after filling the types (no `?` types should remain).

After filling in the types, each expression has two types,
because expressions are implicitly cast to whatever type is needed.
The two types are the same when no casting needs to be done.
For example, if you call `putchar(int)` with an argument of type `byte`,
the argument will implicitly cast to type `int`.

Checking the types before codegen makes the codegen step simpler,
but storing the types into the AST is a bit of a weird design:
an instance of `struct AstExpression` can be either typed or untyped.
It is this way because the codegen step needs to know what type everything has
(to cast signed and unsigned integers correctly, for example; they are the same type in LLVM IR).
Another alternative would be to create a separate "typed AST" that is then converted into LLVM IR,
but in my experience it results in a lot of duplication.

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

Sometimes (very rarely) the fuzzer discovers a bug that hasn't been caught with tests:

```
$ ./fuzzer.sh
```

TODO:
- Upload this to my personal server
- Write syntax spec once syntax seems relatively stable
- Some kind of conversion to bool thingy for if statements: `if some_pointer:`, `if some_integer:`
- `elif`,`else`
- `not`,`and`,`or`
- `while`, `for init; cond; incr:`, maybe `do`-`while`?
- `NULL` pointer
- operators: `+` `-` `*` `/` `<` `>` `==`
- `++` and `--`
    - There's now quite a lot of overlap between expressions and statements.
        Do something about it?
- JIT so that you don't need to go through a compile step
- REPL, if possible?
- Everything else...?
    - Arrays
    - Structs
    - Enums
    - A reasonable way to import structs from C (not just functions).
        - I don't like the zig/rust things that attempt to parse header files and get confused by macros. Something else?
        - A good combination of odd corner cases for testing is probably `struct stat` and the `stat()` function.
    - Self-hosted compiler??!?!
