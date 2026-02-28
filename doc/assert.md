# Assertions

The `assert some_condition` statement does nothing if `some_condition` is `True`
and crashes the program with an error message if `some_condition` is `False`.
The condition must be a `bool`.

For example:

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


## The `jou_assert_fail_handler` global variable

When an assertion fails (that is, the condition of an `assert` statement is `False`),
your program prints the error message shown above
and stops with [exit code](tutorial.md#main-function-and-binaries) 1.
This can be customized with the global variable `jou_assert_fail_handler` defined in `stdlib/assert.jou`.
By default, it is `NULL`, but you can also set it to a function
that takes the following arguments and returns nothing:
- assertion as a string of code (`byte*`)
- path of file with the failing assert (`byte*`)
- line number (`int`)

For example:

```python
import "stdlib/assert.jou"
import "stdlib/io.jou"

def my_handler(assertion: byte*, path: byte*, lineno: int) -> None:
    printf("My handler called!!! %s %s %d\n", assertion, path, lineno)

def main() -> int:
    jou_assert_fail_handler = my_handler

    # Output: My handler called!!! 1 + 2 == 4 test.jou 12
    # Output: Assertion '1 + 2 == 4' failed in file "test.jou", line 12.
    assert 1 + 2 == 4
    return 0
```

Note that the default error message is still printed,
but `jou_assert_fail_handler()` is called first.
This means that if you want to disable the default error message,
you can exit the program in your handler function:

```python
import "stdlib/assert.jou"
import "stdlib/io.jou"

def my_handler(assertion: byte*, path: byte*, lineno: int) -> None:
    printf("oopsie whoopsie\n")
    exit(1)

def main() -> int:
    jou_assert_fail_handler = my_handler
    assert 1 + 2 == 4  # Output: oopsie whoopsie
    return 0
```


## The `_jou_assert_fail()` function

As a user of Jou, you might not need to know anything about the `_jou_assert_fail()` function,
but it's documented here in case you see it in a debugger or an error message.

An assertion like `assert foo` is basically same as:

```python3
if not foo:
    _jou_assert_fail("foo", "filename.jou", 123)
```

Here `_jou_assert_fail` is a function defined in [stdlib/assert.jou](../stdlib/assert.jou).
The error message that tells you to `import "stdlib/assert.jou"` appears
if the Jou compiler doesn't find it.

It is technically possible to define your own `_jou_assert_fail()` function
to be called when an assertion fails, but then
you cannot import `stdlib/assert.jou`,
because your program would have two functions named `_jou_assert_fail()`
and you would get a linker error.
You would also get the same problem if you import anything else that imports `stdlib/assert.jou`.
That's why the `jou_assert_fail_handler` variable [documented above](#the-jou_assert_fail_handler-global-variable) was added
as a way to control what `_jou_assert_fail()` does.
