# Keywords

This file documents what each keyword in the Jou language does.


## `and`

`foo and bar` evaluates to `True` if `foo` and `bar` are both True.
Both `foo` and `bar` must be `bool`s, and `bar` will not be evaluated at all if `foo` is `False`.


## `array_count`

Use `array_count(array)` to get the number of elements in [an array](types.md#pointers-and-arrays) as `int`.
The parentheses are optional.
The number of elements of an array is always known at compile time, and in fact,
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

    # These will not run, because the failing assertion stopped the program.
    printf("Hello\n")
    return 0
```

You need to import [stdlib/assert.jou](../stdlib/assert.jou) to use `assert`.
The compiler will tell you to do so if you forget it:

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

A `const` statement can be decorated with `@public` so that it can be [imported](import.md) into other files:

```python
@public
const MAX_NUMBER_OF_THINGS: int = 100
```


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

**The first**, most common use for `declare` is to declare functions.
This means telling the compiler that a function exists without defining it.
For example, in the example below, the `declare` statement means that
there is a function `puts()`, and it takes a string.
The `puts()` function is actually defined in [C's standard library](tutorial.md#cs-standard-library-libc),
and that's why the code compiles and runs even though it doesn't define `puts()`.

```python3
# No import statements!

declare puts(string: byte*) -> int

def main() -> int:
    puts("Hello")  # Output: Hello
    return 0
```

A `declare` statement can be decorated with `@public` so that it can be [imported](import.md) into other files.

For many more examples of declaring functions, look at [stdlib/io.jou](../stdlib/io.jou) or other stdlib files.

The second way to use `declare` is `declare global`.
It tells the compiler that a global variable exists without defining it.
For example, on Linux, [stdlib/io.jou](../stdlib/io.jou) does this
to access the `stdin`, `stdout` and `stderr` variables defined in C's standard library:

```python
declare global stdin: FILE*
declare global stdout: FILE*
declare global stderr: FILE*
```

A `declare global` statement can be decorated with `@public`, but this is rarely needed.


## `def`

TODO: not documented yet, sorry :(


## `double`

TODO: not documented yet, sorry :(


## `elif`

TODO: not documented yet, sorry :(


## `else`

TODO: not documented yet, sorry :(


## `enum`

TODO: not documented yet, sorry :(


## `enum_count`

TODO: not documented yet, sorry :(


## `False`

TODO: not documented yet, sorry :(


## `float`

TODO: not documented yet, sorry :(


## `for`

TODO: not documented yet, sorry :(


## `funcptr`

TODO: not documented yet, sorry :(


## `global`

TODO: not documented yet, sorry :(


## `if`

TODO: not documented yet, sorry :(


## `import`

TODO: not documented yet, sorry :(


## `int`

TODO: not documented yet, sorry :(


## `int8`

TODO: not documented yet, sorry :(


## `int16`

TODO: not documented yet, sorry :(


## `int32`

TODO: not documented yet, sorry :(


## `int64`

TODO: not documented yet, sorry :(


## `link`

TODO: not documented yet, sorry :(


## `match`

TODO: not documented yet, sorry :(


## `None`

TODO: not documented yet, sorry :(


## `noreturn`

TODO: not documented yet, sorry :(


## `not`

TODO: not documented yet, sorry :(


## `NULL`

TODO: not documented yet, sorry :(


## `or`

TODO: not documented yet, sorry :(


## `pass`

TODO: not documented yet, sorry :(


## `return`

TODO: not documented yet, sorry :(


## `self`

TODO: not documented yet, sorry :(


## `sizeof`

TODO: not documented yet, sorry :(


## `True`

TODO: not documented yet, sorry :(


## `typedef`

TODO: not documented yet, sorry :(


## `uint8`

TODO: not documented yet, sorry :(


## `uint16`

TODO: not documented yet, sorry :(


## `uint32`

TODO: not documented yet, sorry :(


## `uint64`

TODO: not documented yet, sorry :(


## `union`

TODO: not documented yet, sorry :(


## `void`

TODO: not documented yet, sorry :(


## `while`

TODO: not documented yet, sorry :(


## `with`

TODO: not documented yet, sorry :(
