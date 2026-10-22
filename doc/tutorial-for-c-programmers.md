# Jou for C programmers

If you already know C, you know most of the ideas Jou is built on: functions,
types, pointers, memory, and the C standard library. This tutorial focuses on
what is different in Jou, and how to read and write Jou code.

Jou looks a little like Python, but it is compiled to native code and behaves
much more like C. It has no preprocessor, and most of its syntax is deliberately
small. This is a quick introduction; see [the Jou tutorial](tutorial.md) for a
slower explanation of low-level concepts such as pointers and memory.


## A small program

A Jou program starts at `main()` just like a C program. The return type is
written after `->`, and code blocks are introduced by a colon and indentation
instead of braces:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("Hello, world!\n")
    return 0
```

Jou compiles the source and runs the resulting executable. `import` brings in
declarations and other Jou code; for example, `stdlib/io.jou` makes the C
standard library's `printf()` available. Jou strings are zero-terminated, so
they can be passed to C functions that expect `char*`.

There are no semicolons. A newline ends a statement, and indentation determines
which statements belong to a function, loop, or conditional. Comments start
with `#`.


## Functions and variables

Use `def` to define a function, and specify its return type with `->`. A
function that does not return a value uses `None`:

```python
import "stdlib/io.jou"

def twice(n: int) -> int:
    return n * 2

def show_twice(n: int) -> None:
    printf("%d\n", twice(n))

def main() -> int:
    show_twice(21)  # Output: 42
    return 0
```

Arguments always have types. A local variable's type is usually inferred from
its initial value; write a type after the variable name when you want to
specify it explicitly. You can also declare an uninitialized variable, for
example `count: int`, and assign it later. As in C, do not read it before it
has been assigned.


## Control flow

`if`, `while`, and `for` use the familiar C-like conditions and operators, but
their bodies are indented. Jou's `for` loop has C-style initialization,
condition, and update clauses:

```python
import "stdlib/io.jou"

def main() -> int:
    for i = 0; i < 3; i++:
        if i % 2 == 0:
            printf("%d is even\n", i)
        else:
            printf("%d is odd\n", i)
    return 0

# Output: 0 is even
# Output: 1 is odd
# Output: 2 is even
```

`True` and `False` are Jou's boolean literals. Conditions must be booleans;
unlike C, an integer is not implicitly treated as true or false.


## Types and casts

Jou's `int` is always a signed 32-bit integer, and `byte` is an unsigned
8-bit integer. These sizes do not depend on the platform or on the C compiler's
choice of `int` size. Use `int64` for a signed 64-bit integer and `intnative`
for an integer with the same size as a pointer. More integer types are listed
in [the type reference](types.md#integers).

Use `as` for an explicit type conversion:

```python
import "stdlib/io.jou"

def main() -> int:
    b = 255 as byte
    printf("%d\n", b)  # Output: 255

    wrapped = 256 as byte
    printf("%d\n", wrapped)  # Output: 0
    return 0
```

Integer overflow wraps around in Jou. A cast to a smaller integer type can
also wrap, as shown above. Jou does not have C's implementation-defined
`char` signedness: `byte` is always unsigned.


## Pointers and arrays

Pointer types use `*`, as in C. `&value` takes an address, `*pointer`
dereferences one, and indexing a pointer works as expected:

```python
import "stdlib/io.jou"

def set_to_42(pointer: int*) -> None:
    *pointer = 42

def main() -> int:
    value = 0
    set_to_42(&value)
    printf("%d\n", value)  # Output: 42
    return 0
```

Fixed-size arrays are written with the size after the element type: `int[3]`.
Array literals use square brackets:

```python
import "stdlib/io.jou"

def main() -> int:
    values: int[3] = [10, 20, 30]
    printf("%d\n", values[1])  # Output: 20
    return 0
```

An array's size is part of its type and is known at compile time. Use a
pointer when working with dynamically sized memory, and use the standard
library's [`List`](../stdlib/list.jou) when you need a growable array.
Jou pointers, like C pointers, do not keep track of the size or lifetime of
the memory they point to. Invalid accesses and use-after-free can cause
[undefined behavior](ub.md).


## Struct-like classes

Jou classes are similar to C structs: they contain fields and do not use
inheritance. A method is a function declared inside a class; its first
parameter is conventionally named `self`.

```python
import "stdlib/io.jou"

class Point:
    x: int
    y: int

    def print(self) -> None:
        printf("(%d, %d)\n", self.x, self.y)

def main() -> int:
    point = Point{x=3, y=4}
    point.print()  # Output: (3, 4)
    return 0
```

The `Point{x=3, y=4}` syntax constructs a value and initializes its fields.
Classes can also be generic, which is useful for reusable data structures.
See [the classes documentation](classes.md) for more.


## Using C libraries

Jou interoperates directly with C. There is no `#include`: import a Jou file
that declares the C functions you need, or write a `declare` yourself. For
example, the I/O import used above exposes `printf()` with a declaration like
this:

```python
declare printf(pattern: byte*, ...) -> int
```

`declare` tells Jou that a function exists elsewhere; it does not define the
function. The linker resolves it from the C library or another linked library.
Jou uses C's standard library, so familiar functions such as `printf`,
`malloc`, and `free` are available through the declarations in `stdlib/`.

See [the C standard library section in the Jou tutorial](tutorial.md#cs-standard-library-libc)
for more on how declarations and imports work.


## Where to go next

- Read [the full Jou tutorial](tutorial.md) for a more gradual introduction to
  pointers, strings, and memory.
- Browse the [standard library](../stdlib/) and [examples](../examples/) for
  idiomatic Jou code.
- Read the [type reference](types.md), [imports guide](imports.md), and
  [undefined behavior guide](ub.md) for more details.
