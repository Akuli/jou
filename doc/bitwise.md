# Bitwise Operations

If you try to calculate the power of a number with `^`, the results will be surprising:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 3 ^ 10)  # Output: 18
    return 0
```

The problem is that `3 ^ 10` does not mean "`3` to the power of `10`".
It means the bitwise XOR of `3` and `10`.

Jou has the following bitwise operations:
- `a & b` is the bitwise AND
- `a | b` is the bitwise OR
- `a ^ b` is the bitwise XOR

Jou also has in-place versions of these operators: `a &= b` does `a = a & b`.

To explain what bitwise XOR and other operations do,
we first need to understand how numbers can be written in binary.


## Binary Numbers

Let's consider what powers of two (1, 2, 4, 8, 16, ...) a number consists of, in decreasing order.
For example:
- 3 consists of 2 and 1. In binary, it is `11`. This means "has 2 and has 1".
- 10 consists of 8 and 2. In binary, it is `1010`. This means "has 8, doesn't have 4, has 2, doesn't have 1".

Let's check whether this is correct so far.
In Jou, you can use the `0b` prefix to write a number in binary (just like in Python).

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 0b11)  # Output: 3
    printf("%d\n", 0b1010)  # Output: 10
    return 0
```

Another way to think about this is that it's just like the usual way of writing numbers,
but we use powers of two (1, 2, 4, 8, ...) instead of powers of 10 (1, 10, 100, ...).
For example, 105 means "has 100, doesn't have 10, has 1 five times",
just like 101 in binary means "has 4, doesn't have 2, has 1".

The number whose powers are being used is called the **base**.
The usual way of writing numbers is base 10, and binary is base 2.
Other commonly used choices are base 8 (octal) and base 16 (hexedacimal).


## Bitwise AND

The result of `a & b` has 1 bits where `a` and `b` both have 1 bits.
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 3 & 10)  # Output: 2
    return 0
```

Reason:

```
      3  = 0b0011
      10 = 0b1010
---------------=---
  3 & 10 = 0b0010 = 2
```

Think of AND as filtering the bits: `3 &` takes the last two bits of a number.

The type of `a & b` is either the type of `a` or the type of `b`.
The smaller type is chosen.
If both types are the same size, unsigned is preferred.
For example, `byte & int` produces `byte`, and `int16 & uint16` produces `uint16`.


## Bitwise OR

(The `|` operator means a different thing when it is used
[after the `case` keyword in a `match` statement](match.md#combining-multiple-cases-with-).)

The result of `a | b` has 1 bits where `a` or `b` (or both) have 1 bits.
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 3 | 10)  # Output: 11
    return 0
```

Reason:

```
      3  = 0b0011
      10 = 0b1010
-------------=-==-
  3 | 10 = 0b1011 = 8+2+1 = 11
```

Think of OR as adding bits: `3 |` sets the last two bits to 1 regardless of what they were originally.

The type of `a | b` is either the type of `a` or the type of `b`.
The bigger type is chosen.
If both types are the same size, unsigned is preferred.
For example, `byte | int` produces `int`, and `int16 | uint16` produces `uint16`.


## Bitwise XOR

XOR is short for "eXclusive OR": `a ^ b` has 1 bits where either `a` or `b` have 1 bits, but not both.
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 3 ^ 10)  # Output: 9
    return 0
```

Reason:

```
      3  = 0b0011
      10 = 0b1010
-------------=--=-
  3 | 10 = 0b1001 = 8+1 = 9
```

Think of XOR as flipping (toggling) bits: `3 ^` flips the last two bits, either from 0 to 1 or from 1 to 0.

The type of `a ^ b` is determined just like for bitwise OR (see above).


## Unsupported/Missing

Bitshift operators `<<` and `>>` don't exist yet.
For now, use multiplication and division.
For example, you can use `x * 8` when you need `x << 3`.

The bitwise invert operator `~a` doesn't exist yet.
For now, you can toggle all bits by applying XOR with a number consisting entirely of ones,
e.g. `x ^ (0xFFFF_FFFF as uint32)` for a 32-bit invert.
(Look below to understand what `0xFFFF_FFFF` does.)

See also [issue #879](https://github.com/Akuli/jou/issues/879).


## Hexadecimal Numbers

If you want to extract the last 8 bits of a number, you can do this:

```python
def main() -> int:
    number = 1234
    last_8_bits = number & 0b11111111
    printf("%d\n", last_8_bits)  # Output: 214
    return 0
```

(The `_` in `0b1111_1111` is ignored, but it makes the number more readable to humans.)

But most experienced programmers would write it like this:

```python
def main() -> int:
    number = 1234
    last_8_bits = number & 0xFF
    printf("%d\n", last_8_bits)  # Output: 214
    return 0
```

Here `0x` means that you are specifying a number using **hexadecimal**, also known as **hex**.
A practical way to understand hexadecimal is that it's a shorter way to specify many bits.
Hexadecimal numbers must consist of 0-9 or A-F (or a-f, it is not case sensitive),
and each character specifies four bits like this:

```
hexadecimal 0 = binary 0000
hexadecimal 1 = binary 0001
hexadecimal 2 = binary 0010
hexadecimal 3 = binary 0011
hexadecimal 4 = binary 0100
hexadecimal 5 = binary 0101
hexadecimal 6 = binary 0110
hexadecimal 7 = binary 0111
hexadecimal 8 = binary 1000
hexadecimal 9 = binary 1001
hexadecimal A = binary 1010
hexadecimal B = binary 1011
hexadecimal C = binary 1100
hexadecimal D = binary 1101
hexadecimal E = binary 1110
hexadecimal F = binary 1111
```

For example, `0xFF` is same as `0b1111_1111`, because each `F` means the bits `1111`.

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 0xFF)         # Output: 255
    printf("%d\n", 0b1111_1111)  # Output: 255

    printf("%d\n", 0xCAFE)                 # Output: 51966
    printf("%d\n", 0b1100_1010_1111_1110)  # Output: 51966

    return 0
```

Another way to think about hexadecimal is that it's just like the usual base-10 way of writing numbers,
but with base 16.
For example, `0xFF` means `F`ifteen 16's and `F`ifteen ones:

```python
def main() -> int:
    printf("%d\n", 0b1111_1111)  # Output: 255
    return 0
```

So we use powers of 16 (1, 16, 256, 4096, ...) instead of powers of 10 (1, 10, 100, ...).
For example, 105 means "has 100, doesn't have 10, has 1 five times",
just like 101 in binary means "has 4, doesn't have 2, has 1".
The number whose powers are being used is called the **base**:
the usual way of writing numbers is base 10, and binary is base 2.

