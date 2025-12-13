# Loops

Jou has only two very simple kinds of loops, a `while` loop and a C-style `for` loop.
This means that even though the loops are simple,
they are used in a number of different ways to achieve many different things.

This file first describes how `while` and `for` loops work,
and then shows many common ways to use them.

You can use [break](keywords.md#break) and [continue](keywords.md#continue)
together with any of the ways to write a loop shown here.


## While Loop

A `while` loop looks like `while condition:` followed by a body.
The condition is an expression that must evaluate to a `bool`.
It works just like in other programming languages:
it evaluates the condition, and if it's `True`, it runs the body and then goes back to evaluating the condition again.

For example:

```python
import "stdlib/io.jou"

def main() -> int:
    # Output: 0
    # Output: 1
    # Output: 2
    i = 0
    while i < 3:
        printf("%d\n", i)
        i++

    # Output: 1
    # Output: 2
    # Output: 3
    i = 0
    while i < 3:
        i++
        printf("%d\n", i)

    return 0
```


## For Loop

A for loop looks like `for init; cond; incr:` followed by a body, where:
- `init` is runs before the loop starts
- `cond` is just like the condition of a `while` loop
- `incr` is runs at the end of each iteration.

Note that there must be semicolons between the three things.
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    # Output: 0
    # Output: 1
    # Output: 2
    for i = 0; i < 3; i++:
        printf("%d\n", i)

    # Output: 1
    # Output: 2
    # Output: 3
    for i = 1; i <= 3; i++:
        printf("%d\n", i)

    return 0
```

See [the documentation on the `for` keyword](keywords.md#for)
for more detais about how exactly `for` loops work.


## Looping through a range of integers

Jou does not have a `range(a, b)` like Python.
If you want to loop from `a` to `b`, either including or excluding `b`,
a `for` loop is usually the best way to do it.
As you can see in the examples above, it is very clear where the loop starts (`i = 0`, `i = 1` or similar)
and whether the end is included or excluded (`i <= 3` vs `i < 3`).

Note that you can use e.g. `i += 2` or `i--` instead of `i++`.
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    # Output: 0
    # Output: 2
    # Output: 4
    # Output: 6
    # Output: 8
    for i = 0; i < 10; i += 2:
        printf("%d\n", i)

    # Output: 5
    # Output: 4
    # Output: 3
    # Output: 2
    # Output: 1
    # Output: 0
    for i = 5; i >= 0; i--:
        printf("%d\n", i)

    return 0
```


## Repeating `n` times

You can use a `for` loop:

```python
import "stdlib/io.jou"

def main() -> int:
    # Output: Hello
    # Output: Hello
    # Output: Hello
    # Output: Hello
    # Output: Hello
    for i = 0; i < 5; i++:
        printf("Hello\n")

    return 0
```

Alternatively, if you don't want to create an index variable `i`,
you can use a `while` loop with [the funny `-->` "operator" that is actually `--` followed by `>`](https://stackoverflow.com/q/1642028):

```python
import "stdlib/io.jou"

def main() -> int:
    n = 5  # How many times to repeat

    # Output: Hello
    # Output: Hello
    # Output: Hello
    # Output: Hello
    # Output: Hello
    while n --> 0:
        printf("Hello\n")

    return 0
```


## Looping through a list

There are two ways to loop through a list: with indexes, and with pointers.
do not append more items while looping like this, [that doesn't work](lists.md#looping-through-a-list))
For a better explanation of how these loops work,
see [the looping documentation of lists.md](lists.md#looping-through-a-list).

```python
import "stdlib/io.jou"
import "stdlib/list.jou"

def main() -> int:
    numbers = List[int]{}
    numbers.append(123)
    numbers.append(456)
    numbers.append(789)

    # Loop through list with indexes
    # Output: 123
    # Output: 456
    # Output: 789
    for i = 0; i < numbers.len; i++:
        printf("%d\n", numbers.ptr[i])

    # Loop through list with pointers
    # Output: 123
    # Output: 456
    # Output: 789
    for p = numbers.ptr; p < numbers.end(); p++:
        printf("%d\n", *p)

    return 0
```


## Looping through a list backwards

Use a `for` loop with indexes and `i--`:

```python
import "stdlib/io.jou"
import "stdlib/list.jou"

def main() -> int:
    numbers = List[int]{}
    numbers.append(123)
    numbers.append(456)
    numbers.append(789)

    # Output: 789
    # Output: 456
    # Output: 123
    for i = numbers.len - 1; i >= 0; i--:
        printf("%d\n", numbers.ptr[i])

    return 0
```

Alternatively, if you want to clear the list as you go, you can use
[the `pop()` method](lists.md#deleting-the-last-list-element) in a `while` loop like this:

```python
import "stdlib/io.jou"
import "stdlib/list.jou"

def main() -> int:
    numbers = List[int]{}
    numbers.append(123)
    numbers.append(456)
    numbers.append(789)

    # Output: 789
    # Output: 456
    # Output: 123
    while numbers.len > 0:
        printf("%d\n", numbers.pop())

    return 0
```

It is also possible to loop through a list backwards with pointers,
but the simplest ways to write it don't work with an empty list, so it's not recommended.


## Looping through an array

Looping through an array with indexes:

```python
import "stdlib/io.jou"

def main() -> int:
    array = [123, 456, 789]

    # Output: 123
    # Output: 456
    # Output: 789
    for i = 0; i < array_count(array); i++:
        printf("%d\n", array[i])

    return 0
```

Looping through an array with pointers:

```python
import "stdlib/io.jou"

def main() -> int:
    array = [123, 456, 789]

    # Output: 123
    # Output: 456
    # Output: 789
    for p = &array[0]; p < &array[array_count(array)]; p++:
        printf("%d\n", *p)

    return 0
```

Looping through an array backwards with indexes:

```python
import "stdlib/io.jou"

def main() -> int:
    array = [123, 456, 789]

    # Output: 789
    # Output: 456
    # Output: 123
    for i = array_count(array) - 1; i >= 0; i--:
        printf("%d\n", array[i])

    return 0
```

Looping through an array backwards with pointers:

```python
import "stdlib/io.jou"

def main() -> int:
    array = [123, 456, 789]

    # Output: 789
    # Output: 456
    # Output: 123
    for p = &array[array_count(array) - 1]; p >= &array[0]; p--:
        printf("%d\n", *p)

    return 0
```

If you don't use all elements of the array, and you instead store the number of used elements in a variable,
you can use that instead of `array_count(array)` above.
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    array: int[10]
    array[0] = 123
    array[1] = 456
    array[2] = 789
    array_len = 3

    # Output: 123
    # Output: 456
    # Output: 789
    for p = &array[0]; p < &array[array_len]; p++:
        printf("%d\n", *p)

    return 0
```


## Looping through a string

Looping through the bytes of a string is similar to looping through an array,
except that instead of `i < array_count(array)` or `i < array_len`,
you check for [the zero byte that ends a string](tutorial.md#more-about-strings):

```python
import "stdlib/io.jou"

def main() -> int:
    string = "Hello"

    # Output: H
    # Output: e
    # Output: l
    # Output: l
    # Output: o
    for i = 0; string[i] != '\0'; i++:
        printf("%c\n", string[i])

    # Output: H
    # Output: e
    # Output: l
    # Output: l
    # Output: o
    for p = &string[0]; *p != '\0'; p++:
        printf("%c\n", *p)

    return 0
```

This loops through the string [one byte at a time, which is not necessarily one character at a time](tutorial.md#characters).
That is good enough for most use cases.
Use [stdlib/utf8.jou](../stdlib/utf8.jou) if you really need to work with Unicode characters instead of bytes.
