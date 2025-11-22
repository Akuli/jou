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
