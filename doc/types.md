# Types

This page documents all types in the Jou programming language.


## Integers

| Name      | Example   | Size              | Signed?   | How to print  | Min value                     | Max value                     |
|-----------|-----------|-------------------|-----------|---------------|-------------------------------|-------------------------------|
| `byte`    | `'a'`     | 1 byte (8 bits)   | unsigned  | `%d`          | `0`                           | `255`                         |
| `short`   | `1234S`   | 2 bytes (16 bits) | signed    | `%d`          | `-32_768`                     | `32_767`                      |
| `int`     | `1234`    | 4 bytes (32 bits) | signed    | `%d`          | `-2_147_483_648`              | `2_147_483_647`               |
| `long`    | `1234L`   | 8 bytes (64 bits) | signed    | `%lld`        | `-9_223_372_036_854_775_808`  | `9_223_372_036_854_775_807`   |

Integers in Jou code may contain underscores.
They are ignored, but they often make large numbers much more readable.

Integers wrap around if you exceed their minimum/maximum values.
For example, `(0 as byte) - (1 as byte)` produces `255 as byte`.
It is not possible to invoke [UB](ub.md) by overflowing integers.

When printing, `%d` can be used for anything smaller than `int`,
because numbers smaller than `int` are automatically converted to `int`.

Support for other combinations of sizes and signed/unsigned is planned, but not implemented.
See [issue #164](https://github.com/Akuli/jou/issues/164).


## Floating-point numbers

| Name      | Example   | Size              | How to print  |
|-----------|-----------|-------------------|---------------|
| `float`   | `12.34f`  | 4 bytes (32 bits) | `%f`          |
| `double`  | `12.34`   | 8 bytes (64 bits) | `%f`          |

When printing, floats are converted to doubles automatically,
so the same `%f` works for both floating-point types.


## Pointers and Arrays

| Name                          | Example                       | Size                      | Description                                       |
|-------------------------------|-------------------------------|---------------------------|---------------------------------------------------|
| `T[n]` where `T` is any type  | `[1, 2, 3]` (type `int[3]`)   | `n` times the size of `T` | Array of `n` elements (`n` known at compile time) |
| `T*` where `T` is any type    | `"hello"` (type `byte*`)      | 8 bytes (64 bits)         | Pointer to `T`                                    |
| `void*`                       | `NULL`                        | 8 bytes (64 bits)         | Pointer to anything                               |

See [pointers in the Jou tutorial](tutorial.md#pointers) if you are not already familiar with pointers.

An array is simply `n` instances of type `T` next to each other in memory.
The array length `n` must be known at compile time,
because in Jou, the compiler knows the sizes of all types.
If you want a dynamic array size, use a heap allocation (`malloc` + `free`) instead.
Unfortunately, heap allocations are currently not documented.
See [issue #676](https://github.com/Akuli/jou/issues/676).

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


# Casts

Jou has two kinds of casts:
- **Implicit casts** are done basically whenever a value must be of a specific type.
    For example, you can pass an `int` to a function that expects a `long` argument,
    because function arguments are cast implicitly.
- **Explicit casts** are done with the `as` keyword, e.g. `0 as byte`.

The allowed implicit casts are:
- Array to pointer: `T[N]` (where `T` is a type and `N` is an integer) casts implicitly to `T*` or `void*`
- Integer to integer: [Integer type](types.md#integers) `T1` casts implicitly to another integer type `T2`
    if the size of `T1` is smaller than the size of `T2`,
    and this is not a "signed → unsigned" cast.
    For example, `byte` casts implicitly to `int` (8-bit unsigned → 32-bit signed),
    and `int` casts implicitly to `long` (32-bit signed → 64-bit signed).
- `float` casts implicitly to `double`.
- Any integer type (signed or unsigned) casts implicitly to `float` or `double`.
- Any pointer type casts implicitly to `void*`, and `void*` casts implicitly to any pointer type.

Explicit casts are:
- Array to pointer: `T[N]` (where `T` is a type and `N` is an integer) casts to `T*` or `void*`.
    Note that `array_of_ints as long*` is a compiler error.
- Any pointer type casts to any other pointer type. This includes `void*` pointers.
- Any number type casts to any other number type.
- Any integer type casts to any enum.
- Any enum casts to any integer type.
- `bool` casts to any integer type and produces zero or one.
    For example, `True as int` is `1`.
    However, integers cannot be cast to `bool`.
    Use an explicit `foo == 1`, `foo != 0`, `foo > 0` or `match foo: ...` depending on what you need.
- Any pointer type casts to `long`. (This gives the memory address as an integer.)
- `long` casts to any pointer type.
