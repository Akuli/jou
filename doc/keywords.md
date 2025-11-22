# Keywords

This file documents what each keyword in the Jou language does.


## `and`

`foo and bar` evaluates to `True` if `foo` and `bar` are both True.
Both `foo` and `bar` must be booleans, and `bar` will not be evaluated at all if `foo` is `False`.


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
