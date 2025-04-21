# Bitwise Operations

If you try to calculate the power of a number with `^`, the results will be surprising:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 3 ^ 10)  # Output: 18
    return 0
```

The problem is that `3 ^ 10` does not mean "`3` to the power of `10`".
It means the bitwise XOR of `2` and `16`.

Jou has the following bitwise operations:
- `a & b` is the bitwise AND
- `a | b` is the bitwise OR
- `a ^ b` is the bitwise XOR

Jou also has in-place versions of these operators: `a &= b` does `a = a & b`.

To explain what bitwise XOR and other operations do,
we first need to understand how numbers can be written in binary.
You basically consider what powers of two (1, 2, 4, 8, 16, ...) the number consists of, in decreasing order.
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


## Bitwise AND

The `&` operator produces a value that has `1` bits in the places where both given values have it.
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

It is often useful to think of AND as filtering the bits:
`3 &` takes the last two bits of a number.


## Bitwise OR

The `&` operator produces a value that has `1` bits in the places where any of the given values have it.
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

Note that the `|` operator means a different thing when it is used
[after the `case` keyword in a `match` statement](match.md#combining-multiple-cases-with-).

It is often useful to think of OR as adding bits:
`3 |` sets the last two bits to 1 regardless of what they were originally.


## Bitwise XOR

XOR is short for "eXclusive OR": `a ^ b` has `1` bits in the places where either `a` or `b` have `1`, but not both.
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

You can think of XOR as flipping (toggling) bits: `3 ^` flips the last two bits, either from 0 to 1 or from 1 to 0.


## Unsupported/Missing

Bitshift operators `<<` and `>>` don't exist yet.
For now, use multiplication and division.
For example, you can use `x * 8` when you need `x << 3`.

The bitwise invert operator `~a` doesn't exist yet.
For now, you can XOR with a number consisting entirely of ones,
e.g. `x ^ (0xFFFF_FFFF as uint32)` for a 32-bit invert.

See also [issue #879](https://github.com/Akuli/jou/issues/879).
