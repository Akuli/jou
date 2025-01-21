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
| `float`   | `1.2f`    | 4 bytes (32 bits) | `%f`          |
| `double`  | `1234S`   | 8 bytes (64 bits) | `%f`          |

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

A void pointer (`void*`) is used when you don't want the compiler to know
the type of the object being pointed at.
You can use void pointers whenever a different pointer type is expected.
However, you can't do e.g. `pointer++` or `*pointer` or `pointer[index]` with a void pointer,
because to do that, the compiler would need to know the size of the underlying value.


## Other types

| Name      | Example                       | Size                              |
|-----------|-------------------------------|-----------------------------------|
| a class   | see [classes.md](classes.md)  | total size of members + padding   |
| an enum   | see [enums.md](enums.md)      | 4 bytes (32 bits)                 |
| `bool`    | `True`, `False`               | 1 byte (8 bits)                   |

Internally, enums are just `int`s.
This means that if you cannot have more than `2147483647` members in the same enum,
but if this ever becomes a problem, you're probably doing something wrong :)

Internally, a `bool` is one byte (zero or one),
because byte is the smallest unit that computers like to work with.
This doesn't matter as much as you might think, but as usual,
please create an issue if this becomes a problem for you.
