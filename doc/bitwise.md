# Bitwise Operations

If you try to calculate the power of a number with `^`, the results will be surprising:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 3 ^ 10)  # Output: 9
    return 0
```

The problem is that `3 ^ 10` does not mean "`3` to the power of `10`".
It means the bitwise XOR of `3` and `10`.

Jou has the following bitwise operations:
- `a & b` is the bitwise AND
- `a | b` is the bitwise OR
- `a ^ b` is the bitwise XOR
- `a << b` is the bitwise shift left
- `a >> b` is the bitwise shift right
- `~a` is the bitwise NOT

Jou also has in-place versions of these operators: `a &= b` does `a = a & b`.
There is no in-place version of `~`.

To explain what bitwise XOR and other operations do,
we first need to understand how numbers can be written in binary.


## Binary Numbers

Let's consider what powers of two (1, 2, 4, 8, 16, ...) a number consists of, in decreasing order.
For example:
- 3 consists of 2 and 1. In binary, it is `11`. This means "has 2 and has 1".
- 10 consists of 8 and 2. In binary, it is `1010`. This means "has 8, doesn't have 4, has 2, doesn't have 1".

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
Other commonly used choices are base 8 (octal) and base 16 (hexadecimal).


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
  3 ^ 10 = 0b1011 = 8+2+1 = 11
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


## Bitwise Shift Left

The result of `a << b` is the number `a` with all bits moved left by the value of `b`.
Zero bits are added to the right and bits on the left are removed.
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 7 << 1)  # Output: 14
    return 0
```

Reason:

```
  7  =  0b110
  14 = 0b1100
```

Mathematically, `number << 1` is same as `number * 2`.
Similarly, `number << 2` multiplies by 4, `number << 3` multiplies by 8, `number << 4` multiplies by 16 and so on.
This is also true for negative numbers:

```python
import "stdlib/io.jou"

def main() -> int:
    # This calculates (-3) * 8
    printf("%d\n", (-3) << 3)  # Output: -24
    return 0
```

In Jou (unlike in C and C++), it is not possible to get [Undefined Behavior](ub.md) by doing a bitshift.
If you shift by a very large amount, all the bits simply get replaced by zeros.
You will also get zero if you shift by a negative amount.
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 15 << 12345)  # Output: 0
    printf("%d\n", 15 << -1)  # Output: 0
    return 0
```

The result of `a << b` is always the same type as `a`.
Bits are thrown away if they don't fit within the type of `a`.
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 'a' << 0)  # Output: 97
    printf("%d\n", 'a' << 1)  # Output: 194
    printf("%d\n", 'a' << 2)  # Output: 132
    printf("%d\n", 'a' << 3)  # Output: 8
    printf("%d\n", 'a' << 4)  # Output: 16
    return 0
```

Reason:

```
'a' = 97 as byte = 0b01100001 =  64 + 32 + 1 = 97
'a' << 1         = 0b11000010 = 128 + 64 + 2 = 194
'a' << 2         = 0b10000100 =      128 + 4 = 132
'a' << 3         = 0b00001000 =            8 = 8
'a' << 4         = 0b00010000 =           16 = 16
```

To make this less annoying,
[type inference](types.md#type-inference) works so that
if the compiler expects `a << b` to be of some type,
it also expects `a` to be of that type.

For example, below `1` becomes an `uint64` when `1 << 63` is annotated as `uint64`.
Note that [you need `%llu` to properly print an `uint64`](types.md#integers).

```python
import "stdlib/io.jou"

def main() -> int:
    a: int = 1 << 63   # doesn't fit
    printf("%d\n", a)  # Output: 0

    b: uint64 = 1 << 63
    printf("%llu\n", b)  # Output: 9223372036854775808

    return 0
```


## Bitwise Shift Right

The result of `a >> b` is the number `a` with all bits moved right by the value of `b`.
Zero bits are added to the left and bits on the right are removed.
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 15 >> 1)  # Output: 7
    return 0
```

Reason:

```
  15 = 0b1101
  7  =  0b110
```

Mathematically, `number >> 1` is same as `number / 2` (unless `number` is negative, see below).
Similarly, `number >> 2` divides by 4, `number >> 3` divides by 8, `number >> 4` divides by 16 and so on.
(Jou's `/` operator is floor division when used with integers, so `15.0 / 2.0 == 7.5` but `15 / 2 == 7`.
Feel free to [create an issue on GitHub](https://github.com/Akuli/jou/issues/new) to discuss this if you want.)

In Jou (unlike in C and C++), it is not possible to get [Undefined Behavior](ub.md) by doing a bitshift.
If you shift by a very large amount, all the bits simply get replaced by zeros.
You will also get zero if you shift by a negative amount.
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 15 >> 12345)  # Output: 0
    printf("%d\n", 15 >> -1)  # Output: 0
    return 0
```

Shifting a negative number produces surprising results,
because Jou also shifts the sign bit out of its place.
The sign bit is the first bit in a signed number,
and it is 1 when the number is negative.

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", (-1) >> 1)  # Output: 2147483647
    return 0
```

The type of `a >> b` is simply the type of `a`,
and the type of `b` does not affect that.


## Bitwise Not

The result of `~a` is the value of `a` with all bits flipped.
This is also known as bitwise negation and bitwise inverting.
See [XOR](#bitwise-xor) if you want to flip only some of the bits in a number.

For example:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", ~(10 as byte))  # Output: 245
    return 0
```

Reason:

```
   10 = 0b00001010
  245 = 0b11110101
```

Using `~` with signed types produces surprising results, because it also flips the sign bit.
The sign bit is the first bit in a signed number,
and it is 1 when the number is negative.
Therefore negative values become non-negative and non-negative values become negative.
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", ~(3 as int8))  # Output: -4
    printf("%d\n", ~3)            # Output: -4
    return 0
```

Reason (for the 8-bit `~(3 as int8)` example above, works the same way with 32-bit `int`):

```
  3 (signed)    = 3 (unsigned)   = 00000011
  2 (signed)    = 2 (unsigned)   = 00000010
  1 (signed)    = 1 (unsigned)   = 00000001
  0 (signed)    = 0 (unsigned)   = 00000000
  -1 (signed)   = 255 (unsigned) = 11111111
  -2 (signed)   = 254 (unsigned) = 11111110
  -3 (signed)   = 253 (unsigned) = 11111101
  -4 (signed)   = 252 (unsigned) = 11111100
```

Here's one way to think about what happened above.
As you can see, `-1` is the value where all bits are 1.
On the other hand, `x + (~x)` also has all bits set to 1,
because each bit comes from either `x` or `~x`.
Therefore `x + (~x) == -1` for any signed value `x`.
For example, we got `-4` because `3 + (-4) == -1`.

The type of `~a` is always same as the type of `a`.


## Hexadecimal Numbers

If you want to extract the last 8 bits of a number, you can do this:

```python
import "stdlib/io.jou"

def main() -> int:
    number = 1234
    last_8_bits = number & 0b1111_1111
    printf("%d\n", last_8_bits)  # Output: 210
    return 0
```

(The `_` in `0b1111_1111` is ignored, but it makes the number more readable to humans.)

Most experienced programmers would write it like this:

```python
import "stdlib/io.jou"

def main() -> int:
    number = 1234
    last_8_bits = number & 0xFF
    printf("%d\n", last_8_bits)  # Output: 210
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

    printf("%d\n", 0x4d2)             # Output: 1234
    printf("%d\n", 0b0100_1101_0010)  # Output: 1234

    return 0
```

Another way to think about hexadecimal is that it is base 16
where `A` means 10, `B` means 11, `C` means 12, `D` means 13, `E` means 14 and `F` means 15.
This means that instead of powers of 2 (1, 2, 4, 8, ...) or 10 (1, 10, 100, ...), we use powers of 16 (1, 16, 256, 4096, ...).
For example, `0x4d2` means 256 repeated `4` times, 16 repeated `13` (`d`) times, and 1 repeated `2` times:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 0x4d2)                # Output: 1234
    printf("%d\n", 4*256 + 13*16 + 2*1)  # Output: 1234
    return 0
```
