# Keywords

This file documents what each keyword in the Jou language does.


## `and`

The result of `foo and bar` is `True` if `foo` and `bar` are both `True`, and otherwise `False`.
Both `foo` and `bar` must be `bool`s, and `bar` is not evaluated at all if `foo` evaluates to `False`.

**See also:** [or](#or), [not](#not)


## `array_count`

Use `array_count(array)` to get the number of elements in [an array](types.md#pointers-and-arrays) as `int`.
The parentheses are optional.
The number of elements in an array is always known at compile time, and in fact,
the `array` is not evaluated when the program runs.

For example:

```python
import "stdlib/io.jou"

def main() -> int:
    array: int[10]
    printf("%d\n", array_count(array))  # Output: 10
    printf("%d\n", array_count array)  # Output: 10
    return 0
```

Another way to get the size of an array is to do `sizeof(array) / sizeof(array[0])`.
For example, in the above example, this would calculate `40 / 4`,
because [the array is just 10 `int`s next to each other](types.md#pointers-and-arrays)
and [each `int` is 4 bytes](types.md#integers).

Unlike the `sizeof` trick, `array_count(array)` will fail to compile
if you accidentally call it on a pointer:

```python
def main() -> int:
    x = 123
    n = array_count(&x)  # Error: array_count must be called on an array, not int*
```


## `as`

The `as` keyword does an explicit cast. See [the documentation on casts](types.md#casts).


## `assert`

The `assert some_condition` statement does nothing if `some_condition` is `True`
and crashes the program with an error message if `some_condition` is `False`.
The condition must be a `bool`.

```python
import "stdlib/assert.jou"
import "stdlib/io.jou"

def main() -> int:
    x = 1
    assert x == 1   # does nothing
    assert x > 5    # Output: Assertion 'x > 5' failed in file "test.jou", line 7.

    # These don't run, because the failing assertion stopped the program.
    printf("Hello\n")
    return 0
```

You need to import [stdlib/assert.jou](../stdlib/assert.jou) to use `assert`.
The compiler tells you what to do if you forget it:

```python
def main() -> int:
    assert 1 + 2 == 3  # Error: you must import "stdlib/assert.jou" to use the assert keyword
```

When an assertion fails (that is, the condition of an `assert` statement is `False`),
the `_jou_assert_fail()` function is called with some information about the `assert` statement
(file name, line number, assertion code as a string).
It prints the error message and stops the program with [exit code](tutorial.md#main-function-and-binaries) 1.

Instead of importing [stdlib/assert.jou](../stdlib/assert.jou),
it is technically possible to define your own `_jou_assert_fail()` function
to be called when an assertion fails.
However, this would mean that you cannot import anything else that imports `stdlib/assert.jou`,
because then your program would have two functions named `_jou_assert_fail()` and you would get a linker error.
Please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new)
if you want to define your own `_jou_assert_fail()` function and you run into this problem.


## `bool`

This is [the built-in Boolean type](types.md#other-types).


## `break`

The `break` keyword stops the innermost [while](#while) or [for](#for) loop it's in.
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    # Output: Before 0
    # Output: After 0
    # Output: Before 1
    # Output: After 1
    # Output: Before 2
    # Output: Stop!!!
    for i = 0; i < 10; i++:
        printf("Before %d\n", i)
        if i == 2:
            printf("Stop!!!\n", i)
            break
        printf("After %d\n", i)

    return 0
```


## `byte`

[This is another name for `uint8`.](types.md#integers)


## `case`

This keyword can only be used in `match` statements. See [match.md](match.md).


## `class`

Used to define a class. See [classes.md](classes.md).


## `const`

The `const FOO: SomeType = value` syntax is used to create a name for a compile-time constant.
This is similar to [global variables](#global) except that `const`s cannot be changed.
By convention, constants are usually named with UPPERCASE.

For example:

```python
import "stdlib/io.jou"

const MESSAGE: byte* = "Hello World!"

def main() -> int:
    puts(MESSAGE)   # Output: Hello World!
    return 0
```

The values of `const` constants can be used in various other places too,
such as array sizes and other `const` constants.
For example:

```python
import "stdlib/io.jou"

const THE_ACTUAL_SIZE: int = 123
const SIZE: int = THE_ACTUAL_SIZE

def main() -> int:
    array: int[SIZE]

    # Output: The array has room for 123 ints.
    printf("The array has room for %d ints.\n", array_count(array))

    return 0
```

A `const` statement can be decorated with `@public` so that it can be [imported](imports.md) into other files:

```python
@public
const MAX_NUMBER_OF_THINGS: int = 100
```

**See also:** [global](#global)


## `continue`

The `continue` keyword skips the rest of the body of the innermost [while](#while) or [for](#for) loop it's in.
Note that the last part of a `for` loop (typically `i++` or similar) is **not** skipped.

For example:

```python
import "stdlib/io.jou"

def main() -> int:
    # Output: Hello 0
    # Output: Hello 1
    # Output: Hello 3
    # Output: Hello 4
    for i = 0; i < 5; i++:
        if i == 2:
            continue
        printf("Hello %d\n", i)

    return 0
```


## `declare`

Unlike most other keywords in Jou, this keyword has two meanings.
However, they are easy to distinguish:
`declare global` means a different thing than `declare` followed by anything else.

**The first, most common use** for `declare` is to declare functions.
This means telling the compiler that a function exists without defining it.
For example, in the example below, the `declare` statement means that
there is a function `puts()`, and it takes a string.
The `puts()` function is actually defined in [C's standard library](tutorial.md#cs-standard-library-libc),
and that's why the code compiles and runs even though it doesn't define `puts()`.

```python
# No import statements!

declare puts(string: byte*) -> int

def main() -> int:
    puts("Hello")  # Output: Hello
    return 0
```

The last argument of a function can be literally `...` when declaring.
This means that the function is **variadic**; that is,
it accepts zero or more arguments of basically any type where you wrote the `...`.
This is how `printf()` is declared in [stdlib/io.jou](../stdlib/io.jou).

A `declare` statement can be decorated with `@public` so that it can be [imported](imports.md) into other files.

For many more examples of declaring functions, look at [stdlib/io.jou](../stdlib/io.jou) or other stdlib files.

**The second, rarely needed** way to use `declare` is `declare global`.
It tells the compiler that a global variable exists without defining it.
For example, on Linux, [stdlib/io.jou](../stdlib/io.jou) does this
to access the `stdin`, `stdout` and `stderr` variables defined in C's standard library:

```python
declare global stdin: FILE*
declare global stdout: FILE*
declare global stderr: FILE*
```

A `declare global` statement can be decorated with `@public`, but this is rarely needed.

**See also:** [def](#def), [global](#global), [None](#none), [noreturn](#noreturn)


## `def`

The `def` keyword defines a function, or if placed inside a class, it [defines a method](classes.md#methods).
For example:

```python
import "stdlib/io.jou"

def print_twice(string: byte*) -> None:
    puts(string)
    puts(string)

def main() -> int:
    # Output: Hello
    # Output: Hello
    print_twice("Hello")
    return 0
```

Compared to e.g. Python, Jou's function and method definitions are quite simple.
For example, there are no keyword-only arguments, positional-only arguments or default values:
just argument names and their types.

It is currently not possible to define a variadic function like `printf()`,
but it is possible to [declare](#declare) a variadic function.

**See also:** [declare](#declare), [None](#none), [noreturn](#noreturn)


## `double`

This is [the built-in `double` type](types.md#floating-point-numbers).


## `elif`

The `elif` keyword is similar to `else if` in languages like C. For example, consider the following:

```python
if foo:
    ...
else:
    if bar:
        ...
    else:
        if baz:
            ...
        else:
            ...
```

With `elif`, this can be written much more cleanly:

```python
if foo:
    ...
elif bar:
    ...
elif baz:
    ...
else:
    ...
```

**See also:** [if](#if), [else](#else), [match](#match)


## `else`

This keyword is used with [if](#if) statements and it does what any programmer would expect.
It is also used in the ternary expression:
`foo if condition else bar` evaluates to `foo` or `bar` depending on the `condition`.

**See also:** [if](#if), [elif](#elif)


## `enum`

Used to define an enum. See [enums.md](enums.md).


## `enum_count`

Use `enum_count(SomeEnum)` to get the number of members in [an enum](enums.md) as `int`.
For example:

```python
import "stdlib/io.jou"
import "stdlib/mem.jou"

enum MouseButton:
    Left
    Right

def main() -> int:
    states: bool[enum_count(MouseButton)]
    memset(states, 0, sizeof(states))  # set all to False

    states[MouseButton.Right as int] = True
    if states[MouseButton.Right as int]:
        printf("Right-click!\n")  # Output: Right-click!

    return 0
```

Unlike a simple `states = [False, False]`, or `states: bool[2]` followed by the `memset()`,
the above example won't write beyond the end of the `states` array
if someone adds support for a `Middle` mouse button in the future.


## `False`

This is a constant of type [bool](#bool) represented by a zero byte in memory.
This means that if you initialize some memory to zero and interpret it as a `bool`,
you get `False`.

**See also:** [True](#true), [bool](#bool)


## `float`

This is [the built-in `float` type](types.md#floating-point-numbers).


## `for`

The `for` keyword is used to do `for` loops.
A for loop looks like `for init; cond; incr:` followed by a body, where:
- `init` is a statement that runs before the loop starts (typically `i = 0`)
- `cond` is an expression that must evaluate to a `bool`, checked when each iteration begins so that `False` stops the loop
- `incr` is a statement that runs at the end of each iteration, even if [continue](#continue) was used.

Note that there must be semicolons between the three things.

Basically, the following two loops do the same thing:

```python
init
while cond:
    body
    incr

for init; cond; incr:
    body
```

However, this is not quite true if [continue](#continue) is used.

Each of `init`, `cond` and `incr` are optional.
If `cond` is omitted, it defaults to `True`.
As an extreme example, `for ;;:` creates an infinite loop,
but `while True:` is much more readable and hence recommended.

Jou does not have a `for thing in collection:` loop,
because different kinds of collections (list, array, string, custom data structure, ...)
would need to be handled differently,
and Jou is a simple language without much hidden magic.

**See also:** [while](#while), [break](#break), [continue](#continue)


## `funcptr`

TODO: not documented yet, sorry :(


## `global`

The `global` keyword is used to create a global variable. For example:

```python
import "stdlib/io.jou"

global x: int

def print_x() -> None:
    printf("%d\n", x)

def main() -> int:
    print_x()  # Output: 0
    x++
    print_x()  # Output: 1
    return 0
```

Note that unlike in Python,
you don't need to use `global` inside a function to modify the global variable.

Currently global variables are always initialized to zero memory,
and it is not possible to specify any other initializing.
For example, numbers are initialized to zero, booleans are initialized to `False` and pointers are initialized to `NULL`.

By default, global variables are private to a file, just like functions.
You can use `@public` if you really want to create a public global variable:

```python
@public
global my_thingy: int
```

**See also:** [const](#const)


## `if`

The `if` keyword has two uses: it can be used in if statements and ternary expressions.

An if statement looks like `if some_condition:` followed by indented code.
The condition must be a `bool`.
After the `if` statement, there may be zero or more [elif](#elif) parts
and then an optional [else](#else) part.
These work in the obvious way.

A ternary expression looks like `foo if condition else bar`.
The `condition` must be a `bool`.
If the `condition` is `True`, then `foo` is evaluated and that is the result of the ternary expression.
If the `condition` is `False`, then `bar` is evaluated and that is the result of the ternary expression.
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    # Output: 7 is odd
    n = 7
    printf("%d is %s\n", n, "even" if n % 2 == 0 else "odd")
    return 0
```

Note that `condition` is evaluated first even though it appears in the middle.

**See also:** [elif](#elif), [else](#else), [match](#match)


## `import`

The `import` keyword is used to access things defined with `@public` in another file.
See [imports.md](imports.md).


## `int`, `int8`, `int16`, `int32`, `int64`

These are [built-in signed integer types](types.md#integers).


## `link`

TODO: not documented yet, sorry :(


## `match`

The `match` statement is basically a way to write long [if](#if)/[elif](#elif)/[else](#else) chains nicely.
See [match.md](match.md).


## `None`

The `None` keyword can be used only after `->` in a function [definition](#def) or [declaration](#declare)
to indicate that the function does not return a value.
It has no other uses.
For example:

```python
import "stdlib/io.jou"

def foo() -> None:
    printf("Hello\n")

def main() -> int:
    foo()
    return 0
```

If you want to say that a value may be missing,
you can use e.g. [NULL](#null) or `-1` depending on the data type.

**See also:** [NULL](#null), [void](#void), [noreturn](#noreturn), [declare](#declare), [def](#def)


## `noreturn`

The `noreturn` keyword can be used only after `->` in a function [definition](#def) or [declaration](#declare).
It means that the function **never returns at all**, not with a value or without a value.
Use [None](#none) instead if you want to say that a function does not return a value.

Basically, `noreturn` should only be used for functions that
do an infinite loop or stop the whole program.

One use case for `noreturn` is error handling functions that stop the program.
For example, consider the following code.
It gives a compiler warning on the `if f == NULL` line:

```python
import "stdlib/io.jou"
import "stdlib/process.jou"

def fail(message: byte*) -> None:
    fprintf(get_stderr(), "Well, it seems like we %s :(\n", message)
    exit(1)

def main() -> int:
    f = fopen("thingy.txt", "r")
    if f == NULL:  # Warning: function 'main' doesn't seem to return a value in all cases
        fail("cannot open file")  # Output: Well, it seems like we cannot open file :(
    else:
        fclose(f)
        return 0
```

If you change the return type of `fail()` from `-> None` to `-> noreturn`,
the warning goes away,
because then the compiler knows that it doesn't need to worry about what happens after calling `fail()`.

**See also:** [None](#none), [return](#return), [declare](#declare), [def](#def)


## `not`

The result of `not foo` is `True` if `foo` is `False`, and `False` if `foo` is `True`.
Type type of `foo` must be `bool`.

**See also:** [and](#and), [or](#or)


## `NULL`

`NULL` is a special pointer that is used as a special "missing" value.
Jou's `NULL` constant has type [`void*`](types.md#pointers-and-arrays),
so it converts implicitly to any other pointer type.

For example, the `strstr()` function declared in [stdlib/str.jou](../stdlib/str.jou)
finds a substring from a string, and returns a pointer to the substring it finds,
or `NULL` for not found:

```python
import "stdlib/io.jou"
import "stdlib/str.jou"

def main() -> int:
    string = "Hello World"
    if strstr(string, "Test") == NULL:
        printf("does not contain Test\n")    # Output: does not contain Test
    return 0
```

The `NULL` pointer is represented in memory as zero bytes.
This means that if you initialize some memory to zero and interpret it as a pointer,
you get `NULL`.

Accessing a NULL pointer [produces UB, and typically crashes the program](ub.md#null-pointer-errors).

**See also:** [None](#none), [void](#void)


## `or`

The result of `foo or bar` is `True` if either `foo` or `bar` is `True`, or both are `True`,
and `False` if neither is `True`.
Both `foo` and `bar` must be `bool`s, and `bar` is not evaluated at all if `foo` evaluates to `True`.

**See also:** [and](#and), [not](#not)


## `pass`

The `pass` statement does nothing, [just like in Python](https://stackoverflow.com/questions/13886168/how-to-use-pass-statement).
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    x = 7

    if x > 10:
        printf("Way too big\n")
    elif x < 0:
        printf("Way too small\n")
    elif x == 5:
        pass  # Just right, no need to print a message
    else:
        printf("Close, but not quite...\n")  # Output: Close, but not quite...

    return 0
```

Without the `pass`, you would get a compiler error,
because the next line after `elif whatever:` must be indented, but the `else` line is not indented.
A comment or blank line is not enough, because the compiler ignores comments and blank lines.

In the above example, you could instead write `elif x != 5`,
but that's not always possible.
For example, [`pass` is often useful with `match` statements](match.md#special-casing-for-enums).


## `return`

The `return` keyword works just like you would expect:
it stops the function it's in,
and in functions that are not defined with `-> None`, it must be followed by a return value.

For example:

```python
import "stdlib/io.jou"

def do_thing_maybe(thing: byte*) -> None:
    if thing[0] == 'f':
        return   # Do nothing if it starts with 'f'
    printf("Hello! %s\n", thing)

def main() -> int:
    do_thing_maybe("foo")  # no output from here
    do_thing_maybe("bar")  # Output: Hello! bar
    do_thing_maybe("baz")  # Output: Hello! baz
    return 0
```

**See also:** [None](#none), [noreturn](#noreturn)


## `self`

The `self` keyword can be used only inside a method,
and it is a pointer to the instance (or the instance itself) whose method is being called.
See [the documentation about methods](classes.md#methods) for details.


## `sizeof`

TODO: not documented yet, sorry :(


## `True`

This is a constant of type [bool](#bool) represented by a 1 byte in memory.

**See also:** [False](#false), [bool](#bool)


## `typedef`

TODO: not documented yet, sorry :(


## `uint8`, `uint16`, `uint32`, `uint64`

These are [built-in unsigned integer types](types.md#integers).


## `union`

TODO: not documented yet, sorry :(


## `void`

This keyword can be used only to specify [the void pointer type](types.md#pointers-and-arrays) by writing `void*`.
Use [None](#none) when a function does not return a value.

**See also:** [None](#none), [NULL](#null)


## `while`

TODO: not documented yet, sorry :(

**See also:** [for](#for), [break](#break), [continue](#continue)


## `with`

This keyword can be used only to [specify a function in a `match` statement](match.md#using-match--with-strcmp-to-match-strings).
