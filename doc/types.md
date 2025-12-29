# Types

This page documents all types in the Jou programming language.


## Integers

Jou has the following signed integer types:

| Name              | Size              | How to print  | Min value                     | Max value                     |
|-------------------|-------------------|---------------|-------------------------------|-------------------------------|
| `int8`            | 1 byte (8 bits)   | `%d`          | `-128`                        | `127`                         |
| `int16`           | 2 bytes (16 bits) | `%d`          | `-32_768`                     | `32_767`                      |
| `int32` or `int`  | 4 bytes (32 bits) | `%d`          | `-2_147_483_648`              | `2_147_483_647`               |
| `int64`           | 8 bytes (64 bits) | `%lld`        | `-9_223_372_036_854_775_808`  | `9_223_372_036_854_775_807`   |
| `intnative`       | 4 or 8 bytes      | `%zd`         | one of the above two values   | one of the above two values   |

And the following unsigned integer types:

| Name              | Size              | How to print  | Min value | Max value                     |
|-------------------|-------------------|---------------|-----------|-------------------------------|
| `uint8` or `byte` | 1 byte (8 bits)   | `%d`          | `0`       | `255`                         |
| `uint16`          | 2 bytes (16 bits) | `%d`          | `0`       | `65_535`                      |
| `uint32`          | 4 bytes (32 bits) | `%u`          | `0`       | `4_294_967_295`               |
| `uint64`          | 8 bytes (64 bits) | `%llu`        | `0`       | `18_446_744_073_709_551_615`  |

Values of integers in Jou code may contain underscores.
They are ignored, but they often make large numbers much more readable.
The minimum and maximum values shown above are also in [stdlib/limits.jou](../stdlib/limits.jou).

For convenience, the most commonly used types have simpler names:
`byte` always means same as `uint8` and `int` always means same as `int32`.

The `intnative` type is defined in [stdlib/intnative.jou](../stdlib/intnative.jou),
not built-in like other integer types shown here.
There is [a better explanation of it in the Jou tutorial](tutorial.md#intnative).
Please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new)
if you need a corresponding `uintnative` type.

When printing, `%d` can be used for anything smaller than 32 bits,
because values smaller than `int` are automatically converted to `int`.
With 32-bit and 64-bit values, you need to be more careful:
use `u` for unsigned, and add `ll` for 64-bit values or `z` for native-size values.

The `ll` in `%lld` and `%llu` is short for "long long",
which is basically C's way to say "64-bit number".

There is also `%ld` and `%lu`,
but I don't recommend using them in Jou code because their behavior on different platforms is surprising:
they print a native-size value on all platforms except that on 64-bit Windows,
they print a 32-bit value.

Integers wrap around if you exceed their minimum/maximum values.
For example, `(0 as byte) - (1 as byte)` produces `255 as byte`.
It is not possible to invoke [UB](ub.md) by overflowing integers.


## Floating-point numbers

| Name      | Example   | Size              | How to print  |
|-----------|-----------|-------------------|---------------|
| `float`   | `12.34f`  | 4 bytes (32 bits) | `%f`          |
| `double`  | `12.34`   | 8 bytes (64 bits) | `%f`          |

When printing, floats are converted to doubles automatically,
so the same `%f` works for both floating-point types.


## Pointers and Arrays

| Name                          | Example                       | Size                          | Description                                       |
|-------------------------------|-------------------------------|-------------------------------|---------------------------------------------------|
| `T[n]` where `T` is any type  | `[1, 2, 3]` (type `int[3]`)   | `n` times the size of `T`     | Array of `n` elements (`n` known at compile time) |
| `T*` where `T` is any type    | `"hello"` (type `byte*`)      | 4 or 8 bytes (32 or 64 bits)  | Pointer to `T`                                    |
| `void*`                       | `NULL`                        | 4 or 8 bytes (32 or 64 bits)  | Pointer to anything                               |

See [pointers in the Jou tutorial](tutorial.md#pointers) if you are not already familiar with pointers.

The size of a pointer is 32 bits on a 32-bit system and 64 bits on a 64-bit system.
In other words, the size of any pointer is the size of [intnative](tutorial.md#intnative):
a pointer is basically an `intnative` that contains the location of something in the computer's memory.

An array is simply `n` instances of type `T` next to each other in memory.
The array length `n` must be known at compile time,
because in Jou, the compiler knows the sizes of all types.
Use [lists](lists.md) if you want an array that grows dynamically as items are added to it.

To get the number of elements of an array as `int`, you can use the [`array_count`](keywords.md#array_count) built-in.

Pointers and arrays can be combined with each other.
For example, `byte[100]*` means a pointer to an array of 100 bytes,
and `int**` means a pointer to a pointer to an integer.

Note that indexes and array sizes are reversed.
For example, `array: int[10][100]` means 100 rows of 10 ints each,
so `array[99][9]` is the bottom right corner, and `array[9][99]` is [UB](ub.md).

A void pointer (`void*`) is used when you don't want the compiler to know
the type of the object being pointed at.
You can use void pointers whenever a pointer is expected.
However, you can't do e.g. `pointer++` or `*pointer` or `pointer[index]` with a void pointer,
because to do that, the compiler would need to know the size of the underlying value.


## Other types

| Name      | Example                       | Size                              |
|-----------|-------------------------------|-----------------------------------|
| a class   | see [classes.md](classes.md)  | total size of members + padding   |
| an enum   | see [enums.md](enums.md)      | 4 bytes (32 bits)                 |
| `bool`    | `True`, `False`               | 1 byte (8 bits)                   |

Once the program is compiled, enums basically become `int`s,
and `bool`s become `byte`s (zero for `False`, one for `True`).

The size of a `bool` is one byte,
because byte is the smallest unit that computers like to work with.
This doesn't matter as much as you might think, but as usual,
please create an issue if this becomes a problem for you.


## Casts

Jou has two kinds of casts:
- **Implicit casts** are done basically whenever a value must be of a specific type.
    For example, you can pass an `int` to a function that expects an `int64` argument,
    because function arguments are cast implicitly.
- **Explicit casts** are done with [the `as` keyword](keywords.md#as), e.g. `0 as byte`.

The allowed implicit casts are:
- Array to pointer: `T[N]` (where `T` is a type and `N` is an integer) casts implicitly to `T*` or `void*`
- Integer to integer: [Integer type](types.md#integers) `T1` casts implicitly to another integer type `T2`
    if the size of `T1` is smaller than the size of `T2`,
    and this is not a "signed → unsigned" cast.
    For example, `byte` casts implicitly to `int` (8-bit unsigned → 32-bit signed),
    and `int` casts implicitly to `int64` (32-bit signed → 64-bit signed).
- `float` casts implicitly to `double`.
- Any integer type (signed or unsigned) casts implicitly to `float` or `double`.
- Any pointer type casts implicitly to `void*`, and `void*` casts implicitly to any pointer type.

Explicit casts are:
- Array to pointer: `T[N]` (where `T` is a type and `N` is an integer) casts to `T*` or `void*`.
    Note that `array_of_ints as int64*` is a compiler error.
- Any pointer type casts to any other pointer type. This includes `void*` pointers.
- Any number type casts to any other number type. This is described in more detail below.
- Any integer type casts to any enum.
- Any enum casts to any integer type.
- `bool` casts to any integer type and produces zero or one.
    For example, `True as int` is `1`.
    However, integers cannot be cast to `bool`.
    Use an explicit `foo == 1`, `foo != 0`, `foo > 0` or `match foo: ...` depending on what you need.
- Any pointer type casts to `int64`. (This gives the memory address as an integer.)
- `int64` casts to any pointer type.
- Anything that type inference is capable of doing (see below).
    For example `123123123123123 as int64` or `"foo" as byte[10]`.


### Casting number to number

This section defines what exactly "any number type casts to any other number type" mentioned above does.

Casting an integer to another integer [wraps around](tutorial.md#byte-int-int64):

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 260 as byte)  # Output: 4
    return 0
```

Casting a `float` or `double` to an integer truncates away the fractional part.
Out-of-range values become the smallest or largest value of the integer type.
The result is zero if the `float` or `double` is [a NaN value](https://en.wikipedia.org/wiki/NaN).
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 1234.5 as byte)  # Output: 255
    printf("%d\n", 24.68 as int)    # Output: 24
    printf("%d\n", -24.68 as int)   # Output: -24

    # Infinity and NaN
    printf("%d\n", (1.0 / 0.0) as int)   # Output: 2147483647
    printf("%d\n", (0.0 / 0.0) as int)   # Output: 0

    return 0
```


## Type Inference

When the Jou compiler is determining the type of a string or integer in the code,
it considers how it is going to be used.

For example, this is an error, because the type of `1000000000000000` is `int` by default:

```python
def main() -> int:
    x = 1000000000000000  # Error: value does not fit into int
    return 0
```

But if you specify a type for the `x` variable,
the compiler *infers* the type of `1000000000000000` based on it:

```python
def main() -> int:
    x: int64 = 1000000000000000  # This works, no errors
    return 0
```

Jou's type inference is implemented in a very simple way
compared to many other languages and compilers.
For example, using the variable later does not affect the inference in any way:

```python
def main() -> int:
    x = 1000000000000000  # Error: value does not fit into int
    y: int64 = x  # Does not affect the type of x
    return 0
```

Jou also uses type inference for strings.
By default, strings are `byte*`, but they can also be inferred as arrays:

```python
import "stdlib/io.jou"

def main() -> int:
    string1 = "foo"             # Type of "foo" is byte* (the default)
    string2: byte[100] = "foo"  # Type of "foo" is byte[100]
    n = sizeof("foo")           # Type of "foo" is byte[4] (smallest possible)

    # Output: 4 bytes are needed for "foo".
    printf("%d bytes are needed for \"%s\".\n", n, string1)

    return 0
```

When the `sizeof` operator is applied to a string,
the compiler chooses the smallest possible array size where the string fits.
Above `"foo"` needs 4 bytes because of [the terminating zero byte](tutorial.md#more-about-strings):
in this context, `"foo"` is same as `['f', 'o', 'o', '\0']`.
