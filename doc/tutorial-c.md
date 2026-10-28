# Jou for C programmers

The purpose of this tutorial is to get you started with Jou
assuming that you already know C.
Once you learn the syntax differences and a few other details,
you will find that Jou is very similar to C,
and you can rely on your existing C knowledge a lot.
As such, this tutorial focuses on the differences between C and Jou.

I recommend [the other tutorial](tutorial.md) if you don't know C yet.


## Hello World!

Consider the following C program:

```c
#include <stdio.h>

int main(void) {
    printf("Hello World\n");
    return 0;
}
```

In Jou, this becomes:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("Hello World\n")
    return 0
```

Here `import` is basically `#include` but fancier, `def` means defining a function, and `-> int` is the return type.
Strings in Jou work exactly like in C, and we are actually calling C's `printf()` function directly.

You can compile this similarly to how you would compile a C program:

```
$ jou -o hello hello.jou
$ ./hello
Hello World
```

If you don't specify an output file, the compiler will run the program immediately:

```
$ jou hello.jou
Hello World
```


## No preprocessor

There is no preprocessor in Jou.
The closest thing to a macro is an [inline function](inline.md) or a [`const` constant](keywords.md#const).


## `import` vs `#include`

[Jou's `import` statement](imports.md) is fancier than C's `#include`, but easier to use.

Similarities with C:
- There is no namespace or overloading magic to rename the functions. Jou libraries typically use a naming prefix, just like C libraries.
- The string after `import` is a file name.
- There are two kinds of imports.
    Imports starting with a dot are relative to the current file like C's `#include "..."`.
    Imports starting with `stdlib/` are relative to where the standard library is like C's `#include <...>`.

Differences from C:
- There are no separate header files.
- Jou's `import` "just works" even with cyclic dependencies.
- The order of `import` statements doesn't matter.
- Everything in Jou is private by default unless `@public` is used.

See any [standard library](../stdlib) file for examples of `@public`.
(Most of the standard library is surprisingly readable.)


## Python-style syntax

Jou relies on indentation to figure out where code blocks end,
and a newline marks the end of a statement.
So instead of `{` you write `:` on the same line,
and you don't write `}` or `;`.

To allow splitting long things on multiple lines, indentation and newlines inside parentheses are ignored:

```python
import "stdlib/io.jou"

def main() -> int:
    # This is why whitespace is ignored inside parentheses
    # Output: Hello World
    printf(
        "Hello %s\n",
        "World",    # <--- In C you can't put a comma here. In Jou you can.
    )

    # This works too
    # Output: Hello World
    printf(
      "Hello %s\n",
            "World",
  )

    return 0
```

A comment starts with `#` instead of `//`.
There's no special syntax for multi-line comments.
Just put a `#` to the start of each line.
The `# Output:` comments on this page are for [joutest](joutest.md).

You don't need parentheses around `if`, `while` and `for` conditions, and you need `elif` instead of `else if`:

```python
import "stdlib/io.jou"

def main() -> int:
    n = 1
    if n == 1:
        printf("one\n")  # Output: one
    elif n == 2:
        printf("two\n")
    else:
        printf("many\n")
    printf("this runs always\n")  # Output: this runs always

    # Output: 5
    # Output: 4
    # Output: 3
    # Output: 2
    # Output: 1
    n = 5
    while n != 0:
        printf("%d\n", n--)

    # Output: 0
    # Output: 1
    # Output: 2
    # Output: 3
    # Output: 4
    for i = 0; i < 5; i++:
        printf("%d\n", i)

    return 0
```

Conditions must be booleans: `while n:` doesn't compile.

Above `n = 1` creates a local variable of type `int`, because the type of `1` is `int`.
You can specify a type:

```python
x: int64 = 0   # now x is a 64-bit integer
```

If you do `x: int` without assigning a value, the variable contains a garbage value like in C.

Use `pass` instead of `{}` to make an empty block:

```python
def do_nothing() -> None:
    pass
```


## `def`, `declare`, `typedef`

The `def` keyword defines a function, and `declare` declares an existing function.
Return type goes after `->`.
Use `-> None` if the function doesn't return anything.
Just like C compilers, the Jou compiler links with C's standard library (libc),
so a declaration is enough to call libc functions like `puts()`.

```python
# Usually you would import "stdlib/io.jou" instead of doing this yourself
declare puts(string: byte*) -> int

def say_hello() -> None:
    puts("hello")

def main() -> int:
    say_hello()  # Output: hello
    return 0
```

In C, this would be:

```c
int puts(char *string);

void say_hello(void) {
    puts("hello");
}

int main(void) {
    say_hello();
    return 0;
}
```


## Function arguments

Arguments use `variable: type` syntax.
For example, if you want to handle command-line arguments, define `main()` like you would in C:

```python
import "stdlib/io.jou"

def main(argc: int, argv: byte**) -> int:
    printf("Program name: %s\n", argv[0])
    for i = 1; i < argc; i++:
        printf("Argument %d: %s\n", i, argv[i])
    return 0
```


## Pointers

Similarities with C:
- No garbage collection or lifetime checks
- Dereferencing with `*`
- Taking address with `&`
- `++` and `--`
- `NULL`
- `void*`

Differences from C:
- Simpler syntax: type `int*` means a pointer to int, and it's used like any other type
- `pointer + integer` is not supported, Jou needs `&pointer[integer]` instead
- Subtracting two pointers is currently more complicated than it should be (see [issue #1397](https://github.com/Akuli/jou/issues/1397))
- Comparing pointers to different memory regions with `<` or `>` is not undefined behavior
- Comparing `NULL` to other pointers with `<` or `>` is not undefined behavior
- No `const` pointers

Example:

```python
import "stdlib/io.jou"
import "stdlib/intnative.jou"

def increment(ptr: int*) -> None:
    (*ptr)++  # parentheses needed, otherwise pointer moves and value is unchanged

def skip3(string: byte*) -> byte*:
    #return string + 3  # valid in C, compiler error in Jou
    return &string[3]

def main() -> int:
    n = 1
    increment(&n)
    printf("%d\n", n)  # Output: 2

    puts(skip3("hello world"))  # Output: lo world

    # Subtracting pointers is a mess
    array: int[10]
    fifth = &array[5]
    third = &array[3]
    diff = ((fifth as intnative) - (third as intnative)) // sizeof(*third)
    printf("diff = %zd\n", diff)  # Output: diff = 2

    return 0
```


## Arrays

Similarities with C:
- An array is nothing more than several things next to each other in memory
- `sizeof(array)/sizeof(array[0])` works (but you would [`//` division](#division), and Jou's [`array_count()` built-in](keywords.md#array_count) is less fragile)
- Arrays convert to a pointer implicitly
- `malloc()` and `free()` (declared in [stdlib/mem.jou](../stdlib/mem.jou)) can be used for arbitrarily long arrays, but it's easy to use them wrong

Differences from C:
- Simpler syntax: type `int[10]` means an array of 10 ints, and it's used like any other type
- Array size always needed, `int[]` is invalid syntax in Jou
- Square brackets denote an array, e.g. `[1, 2, 3]`
- [The `List` class, also known as a dynamic array](lists.md)
- Built-in math operations like `[1, 2, 3] + [4, 5, 6]`

```python
import "stdlib/mem.jou"
import "stdlib/io.jou"
import "stdlib/list.jou"

def main() -> int:
    numbers: int[10]   # contains uninitialized/garbage values
    n = 0
    numbers[n++] = 12
    numbers[n++] = 34
    numbers[n++] = 56
    numbers[n++] = 78
    # Output: 12
    # Output: 34
    # Output: 56
    # Output: 78
    for i = 0; i < n; i++:
        printf("%d\n", numbers[i])

    # This array becomes ['t', 'h', 'i', 'n', 'g', ' ', '%', 'd', '\n', '\0', '\0', '\0', ...]
    format: byte[50] = "thing %d\n"

    things = [1, 2, 3]  # type of things becomes int[3]
    # Output: thing 1
    # Output: thing 2
    # Output: thing 3
    for i = 0; i < array_count(things); i++:
        printf(format, things[i])

    # Math with arrays
    things *= 2
    things += [100, 200, 300]
    printf("%d %d %d\n", things[0], things[1], things[2])  # Output: 102 204 306

    # List is a dynamic array in heap memory (see doc/lists.md)
    list = List[int]{}
    list.append(123)
    list.append(456)
    list.append(789)
    for p = list.ptr; p < list.end(); p++:
        # Output: 123
        # Output: 456
        # Output: 789
        printf("%d\n", *p)
    free(list.ptr)

    return 0
```


## Integer types

For more details, see [the types documentation](types.md).

| C                                             | Jou               | How to print  | Notes                                                             |
|-----------------------------------------------|-------------------|---------------|-------------------------------------------------------------------|
| `char`                                        | -                 | `%d`, `%c`    | C's `char` can be signed or unsigned, Jou is explicit about this  |
| `unsigned char`                               | `byte` or `uint8` | `%d`, `%c`    | Unlike in C, Jou `byte` is always 8-bit unsigned                  |
| `signed char`                                 | `int8`            | `%d`, `%c`    |                                                                   |
| `int`                                         | `int32`           | `%d`          | Unlike in C, Jou `int` is always 32-bit                           |
| `long`                                        | -                 | `%ld`         | C `long` is 32-bit or 64-bit depending on operating system        |
| `long long`                                   | `int64`           | `%lld`        |                                                                   |
| `size_t` <br> `uintptr_t`                     | -                 | `%zu`         | If you really need this, just define your own `uintnative` type   |
| `ssize_t` <br> `intptr_t` <br> `ptrdiff_t`    | `intnative`       | `%zd`         | Jou needs `import "stdlib/intnative.jou"`                         |

Jou intentionally doesn't support platforms where:
- C `char` is not 8 bits
- C `int` is not 32 bits
- C `long long` is not 64 bits
- C `size_t`, `ssize_t`, `intptr_t`, `uintptr_t` and `ptrdiff_t` are not the same size as pointers

So in a project that uses a mix of Jou and C, some of Jou's guarantees also apply in C code.

For some reason, C's `long` is 32 bits (yes, same as `int`) on Windows, but 64-bit on other systems that Jou supports.
Use something like this if you need to interact with C code that uses `long`:

```python
if WINDOWS:
    typedef c_long = int
else:
    typedef c_long = int64
```

Jou's `typedef` is just like C's `typedef`, but with obvious syntax.

In C, signed overflow is undefined. In Jou, integer overflow wraps around and never invokes undefined behavior.


## Booleans

Similarities with C:
- The data type is named `bool`

Differences from C:
- `True` and `False` have uppercase `T` and `F`
- You don't need to import anything to use booleans
- Conversions between `bool` and integers need to be more explicit

```python
import "stdlib/io.jou"

def main() -> int:
    # bool doesn't convert to int implicitly, this would be compiler error
    #x: int = True

    x = True as int  # explicit cast
    printf("%d\n", x)  # Output: 1

    # For convenience, printf() and other variadic functions take booleans directly
    printf("%d %d\n", False, True)  # Output: 0 1

    n = 3
    #while n--:  # would be compiler error, needs explicit "> 0" or "!= 0" to get boolean
    while n-- > 0:
        # Output: hi
        # Output: hi
        # Output: hi
        printf("hi\n")

    return 0
```


## Division

Dividing with `/` always produces a `float` or a `double`. Use `//` for integer division.
Jou's `//` does floor division, not truncating division like C's `/`.
For most use cases, this works better with negative numbers.

```python
import "stdlib/io.jou"

def main() -> int:
    # Output: 0.500000
    printf("%f\n", 1/2)
    # Output: 0
    printf("%d\n", 1//2)

    # Output: -2 -1 -1 -1 -1 0 0 0 0 1 1
    printf(
        "%d %d %d %d %d %d %d %d %d %d %d\n",
        (-5)//4, (-4)//4, (-3)//4, (-2)//4, (-1)//4, 0//4, 1//4, 2//4, 3//4, 4//4, 5//4,
    )

    # Output: 3 0 1 2 3 0 1 2 3 0 1
    printf(
        "%d %d %d %d %d %d %d %d %d %d %d\n",
        (-5)%4, (-4)%4, (-3)%4, (-2)%4, (-1)%4, 0%4, 1%4, 2%4, 3%4, 4%4, 5%4,
    )

    return 0
```


## Function pointers

Similarities with C:
- A function pointer is just the location of a function in memory, and holds no data to be passed as arguments
- A function pointer can be set to `NULL`

Differences from C:
- Different syntax: `funcptr(byte*) -> int` is a type
- The arguments cannot be named, please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new) if you want to name your `funcptr` arguments
- `&` is not used to create a function pointer to a global function (it's optional in C)
- `*` is not used to call a function through a function pointer (it's optional in C)

```python
import "stdlib/io.jou"

def main() -> int:
    the_function: funcptr(byte*) -> int = puts
    the_function("hi")  # Output: hi
    return 0
```


## Classes

I know what you're thinking.
Yes, it's named `class`, not `struct`.
But it's really a lot like a C `struct`, and there's no inheritance or other overly OOP "features".

Similarities with C `struct`:
- A Jou class is just a chunk of memory with no fancy reference counting or garbage collection
- Compiler may insert padding between class fields to satisfy align requirements
- Compiler does not reorder class fields
- No hidden vtables or other compiler magic

Differences from C `struct`:
- No need for a `typedef`
- Classes can contain methods (they are just functions with slightly different syntax and a dot in their name)
- Different initializing syntax: instead of `struct Thing foo = {x = 1};` you do `foo = Thing{x = 1}`
- No arrow syntax like `pointer->member`, the same `.` operator works even with pointers

```python
import "stdlib/io.jou"
import "stdlib/mem.jou"


class Thing:
    x: int
    y: int

    # This is really just a function named "Thing.increment_x"
    #
    # Defining a function whose name contains "." would be invalid syntax, so
    # a method name cannot conflict with a function name.
    def increment_x(self: Thing*) -> None:
        self.x++

    # If not specified, the type of self defaults to Thing*
    # You can also specify "self: Thing" if you need that for some reason.
    def increment_y(self) -> None:
        self.y++


def main() -> int:
    foo = Thing{x = 1}  # initializes y to zero
    printf("%d %d\n", foo.x, foo.y)  # Output: 1 0

    # The same thing, but more explicit
    foo2: Thing  # leaves everything uninitialized
    memset(&foo2, 0, sizeof(foo2))
    foo2.x = 1
    printf("%d %d\n", foo2.x, foo2.y)  # Output: 1 0

    # These automatically pass &foo as the self parameter
    foo.increment_x()
    foo.increment_y()
    printf("%d %d\n", foo.x, foo.y)  # Output: 2 1

    return 0
```


## `match`

[Jou's `match` statement](match.md) is like C's `switch` statement.

Differences from C `switch`:
- Different syntax for combining multiple cases (`|` operator, read it as "or")
- No `break` or implicit fall-through
- `case _` instead of `default`
- You can use `match ... with ...` to specify a comparator function like `strcmp()`

```python
import "stdlib/io.jou"
import "stdlib/str.jou"

def main() -> int:
    n = 5
    match n:
        case 2 | 3 | 5 | 7:
            printf("prime\n")  # Output: prime
        case 1 | 4 | 6 | 8 | 9:
            printf("not prime\n")
        case _:
            printf("too big\n")

    arg = "--help"
    match arg with strcmp:
        case "-h" | "--help":
            printf("Usage: bla bla bla\n")  # Output: Usage: bla bla bla
        case "-V" | "--version":
            printf("PrimeProgram 1.0\n")
        case _:
            printf("Unknown argument \"%s\" (try --help)\n", arg)

    return 0
```

Usually the `|` operator means bitwise OR, but in a `case` expression it gets a different meaning.


## Enums and constants

[Jou enums](enums.md) are a lot like C enums,
except that you can't specify the values (they always start at zero),
and they are accessed through the enum name so you don't need to prefix the individual members.
For many use cases of C enums, such as bitflags or other constants, Jou's `const` is more appropriate.

```python
import "stdlib/io.jou"

enum Selection:
    Foo
    Bar
    Baz

const MY_THING_FOO: int = 0x01
const MY_THING_BAR: int = 0x02
const MY_THING_THINGY: int = 0x04
const MY_THING_LOL: int = 0x08
const MY_THING_MAGIC: int = 0x10

def my_thing(flags: int) -> None:
    if flags & MY_THING_FOO != 0:
        printf("foo")
    if flags & MY_THING_BAR != 0:
        printf("bar")
    if flags & MY_THING_THINGY != 0:
        printf("thingy")
    if flags & MY_THING_LOL != 0:
        printf("lol")
    if flags & MY_THING_MAGIC != 0:
        printf("magic")
    printf("\n")

def main() -> int:
    s = Selection.Bar
    match s:
        case Selection.Foo:
            printf("Foo\n")
        case Selection.Bar:
            printf("Bar\n")  # Output: Bar
        case Selection.Baz:
            printf("Baz\n")

    # Output: 0 1 2
    printf("%d %d %d\n", Selection.Foo as int, Selection.Bar as int, Selection.Baz as int)

    # Output: foothingylol
    my_thing(MY_THING_FOO | MY_THING_THINGY | MY_THING_LOL)

    return 0
```

In C, `==` and `!=` have higher precedence than `&` for some reason,
so `flags & MY_THING_FOO != 0` would get evaluated as `flags & (MY_THING_FOO != 0)`, which is wrong.
In Jou, it means the intended `(flags & MY_THING_FOO) != 0`.
