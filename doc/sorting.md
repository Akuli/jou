# Sorting

TL;DR:

```python
import "stdlib/sort.jou"
import "stdlib/io.jou"
import "stdlib/list.jou"
import "stdlib/mem.jou"
import "stdlib/str.jou"

def compare_lengths(ptr1: byte**, ptr2: byte**) -> int:
    return (strlen(*ptr1) - strlen(*ptr2)) as int

def main() -> int:
    # Sort an array of integers
    # Output: 1
    # Output: 2
    # Output: 3
    # Output: 4
    arr = [4, 1, 3, 2]
    sort_int32(arr, 4)
    for i = 0; i < 4; i++:
        printf("%d\n", arr[i])

    # Sort a list of integers
    # Output: 1
    # Output: 2
    # Output: 3
    # Output: 4
    numbers = List[int]{}
    numbers.append(4)
    numbers.append(1)
    numbers.append(3)
    numbers.append(2)
    sort_int32(numbers.ptr, numbers.len)
    for p = numbers.ptr; p < numbers.end(); p++:
        printf("%d\n", *p)
    free(numbers.ptr)

    # Sort strings
    # Output: Akuli
    # Output: Moosems
    # Output: littlewhitecloud
    # Output: taahol
    strings = ["Akuli", "taahol", "littlewhitecloud", "Moosems"]
    sort_strings(strings, 4)
    for i = 0; i < 4; i++:
        puts(strings[i])

    # Sort with custom comparator function
    # Output: Akuli
    # Output: taahol
    # Output: Moosems
    # Output: littlewhitecloud
    Sorter[byte*]{}.sort(strings, 4, compare_lengths)
    for i = 0; i < 4; i++:
        puts(strings[i])

    return 0
```


## Sorting integers

The `sort_int32()` function from [stdlib/sort.jou](../stdlib/sort.jou)
sorts `int`s that are right next to each other in memory.
Practically, this means that it works with [arrays and lists](lists.md#lists-vs-arrays).
The `sort_int32()` function takes two arguments:
a pointer to the first integer, and the number of integers being sorted.

Here's how `sort_int32()` can be used to sort an array of integers:

```python
import "stdlib/sort.jou"
import "stdlib/io.jou"

def main() -> int:
    array = [3, 1, 2]
    sort_int32(array, 3)

    # Output: 1
    # Output: 2
    # Output: 3
    for i = 0; i < 3; i++:
        printf("%d\n", array[i])

    return 0
```

Note that we need to specify the array length `3` manually.
This is useful, because [arrays often contain unused space for more items at the end](lists.md#lists-vs-arrays).
You can use [the `array_count` built-in](keywords.md#array_count) if you want to sort the whole array.

You can also use `sort_int32()` with a [list](lists.md) of integers:

```python
import "stdlib/sort.jou"
import "stdlib/list.jou"
import "stdlib/mem.jou"
import "stdlib/io.jou"

def main() -> int:
    list = List[int]{}
    list.append(3)
    list.append(1)
    list.append(2)
    sort_int32(list.ptr, list.len)

    # Output: 1
    # Output: 2
    # Output: 3
    for p = list.ptr; p < list.end(); p++:
        printf("%d\n", *p)

    free(list.ptr)
    return 0
```

The `int32` in the function name means `int`.
There are similarly named functions for [all 8 integer types](types.md#integers):
- `sort_int8()`
- `sort_int16()`
- `sort_int32()`
- `sort_int64()`
- `sort_uint8()`
- `sort_uint16()`
- `sort_uint32()`
- `sort_uint64()`

For example, to sort the individual bytes of a string, you can use `sort_uint8()`:

```python
import "stdlib/sort.jou"
import "stdlib/io.jou"
import "stdlib/str.jou"

def main() -> int:
    message: byte[10] = "testing"
    sort_uint8(message, strlen(message))
    puts(message)  # Output: eginstt
    return 0
```


## Sorting strings

There is a `sort_strings()` function:

```python
import "stdlib/sort.jou"
import "stdlib/io.jou"

def main() -> int:
    languages = ["Python", "C", "Jou"]
    sort_strings(languages, 3)
    # Output: C Jou Python
    printf("%s %s %s\n", languages[0], languages[1], languages[2])
    return 0
```


## Sorting with custom comparator function

Suppose we have an array or list of instances of the following class,
and we want to sort them by year:

```python
class Language:
    name: byte*
    year: int
```

Unfortunately, we can't use `sort_strings()`,
because the names of different `Language` instances are not next to each other in memory:
after each name, there is a year.

To do this, we first need to define a custom comparator function
that takes two pointers to `Language` instances and compares their years.
The return values of a comparator function must be:
- `-1` or other negative `int`, if first item is less than second item
- `1` or other positive `int`, if first item is greater than second item
- `0`, if the items are considered equal.

Here's one way to write the comparator function:

```python
def compare_languages(a: Language*, b: Language*) -> int:
    if a.year < b.year:
        return -1
    if a.year > b.year:
        return 1
    return 0
```

Alternatively, because the return values don't need to be exactly `-1` and `1`
but rather anything negative or anything positive,
you can also do this:

```python
def compare_languages(a: Language*, b: Language*) -> int:
    return a.year - b.year
```

Once you have the comparator function and an array of `Language` instances,
the following can be used to sort the array:

```python
Sorter[Language]{}.sort(array, array_len, compare_languages)
```

Here `Sorter[Language]{}` creates a new instance of the `Sorter` class
that sorts lists of `Language` instances.
Currently it needs to be a class to work around limitations of Jou's generics.
In a future version of Jou, this might become a simple function call,
perhaps `sort(array, array_len, compare_languages)`.

Here's a complete example:

```python
import "stdlib/sort.jou"
import "stdlib/io.jou"

class Language:
    name: byte*
    year: int

    def print(self) -> None:
        printf("%s (%d)\n", self.name, self.year)

def compare_languages(a: Language*, b: Language*) -> int:
    return a.year - b.year

def main() -> int:
    languages = [
        Language{name="Python", year=1991},
        Language{name="C", year=1972},
        Language{name="Jou", year=2022},
    ]

    Sorter[Language]{}.sort(languages, 3, compare_languages)

    # Output: C (1972)
    # Output: Python (1991)
    # Output: Jou (2022)
    for i = 0; i < 3; i++:
        languages[i].print()

    return 0
```

If you instead want to sort the languages by name instead of year,
you only need to change the `compare_languages()` function like this:

```python
def compare_languages(a: Language*, b: Language*) -> int:
    return strcmp(a.name, b.name)
```

The return value of `strcmp()` from [stdlib/str.jou](../stdlib/str.jou)
is positive, zero or negative exactly like the comparator function's return value needs to be,
so we can simply return it as is.
