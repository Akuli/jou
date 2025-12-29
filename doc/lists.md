# Lists

TL;DR:

```python
import "stdlib/io.jou"
import "stdlib/list.jou"
import "stdlib/mem.jou"  # For the free() function

def main() -> int:
    # Create empty list
    numbers = List[int]{}

    # Add items to list
    numbers.append(12)
    numbers.append(34)
    numbers.append(56)

    # Loop with indexes
    # Output: 12
    # Output: 34
    # Output: 56
    for i = 0; i < numbers.len; i++:
        printf("%d\n", numbers.ptr[i])

    # Loop with pointers
    # Output: 12
    # Output: 34
    # Output: 56
    for p = numbers.ptr; p < numbers.end(); p++:
        printf("%d\n", *p)

    # Free memory used by the list
    free(numbers.ptr)

    return 0
```


## Lists vs arrays

Jou arrays are just chunks of memory where multiple items are next to each other.
For example, the size of an array of 3 ints is 12 bytes, because each int is 4 bytes.

```python
import "stdlib/io.jou"

def main() -> int:
    arr = [12, 34, 56]

    # Output: Array uses 12 bytes of memory
    printf("Array uses %d bytes of memory\n", sizeof(arr))

    # Output: First item uses 4 bytes of memory
    # Output: Second item uses 4 bytes of memory
    # Output: Third item uses 4 bytes of memory
    printf("First item uses %d bytes of memory\n", sizeof(arr[0]))
    printf("Second item uses %d bytes of memory\n", sizeof(arr[1]))
    printf("Third item uses %d bytes of memory\n", sizeof(arr[2]))

    return 0
```

If you know that you won't need no more than 10 elements,
you can use an array of 10 elements together with an integer that represents the length:

```python
import "stdlib/io.jou"

def main() -> int:
    arr: int[10]
    arr_len = 0

    # Add items to array
    arr[arr_len++] = 12
    arr[arr_len++] = 34
    arr[arr_len++] = 56

    # Output: it has 3 items
    printf("it has %d items\n", arr_len)

    # Output: 12
    # Output: 34
    # Output: 56
    for i = 0; i < arr_len; i++:
        printf("%d\n", arr[i])

    return 0
```

Here `arr[arr_len++]` increments `arr_len` and indexes `arr` with its old value,
so the first `arr[arr_len++]` is `arr[0]`, the next is `arr[1]` and so on.

Arrays are not usable when you want a list that grows as needed without a fixed maximum size.
This is where Jou's `List` comes in.
It allocates more memory automatically when you add more items to it,
just like Python's `list`, Rust's `Vec` and C++ `std::vector`.
With `List` instead of an array, the above example becomes:

```python
import "stdlib/io.jou"
import "stdlib/list.jou"
import "stdlib/mem.jou"

def main() -> int:
    numbers = List[int]{}

    numbers.append(12)
    numbers.append(34)
    numbers.append(56)

    # Output: it has 3 items
    printf("it has %zd items\n", numbers.len)

    # Output: 12
    # Output: 34
    # Output: 56
    for i = 0; i < numbers.len; i++:
        printf("%d\n", numbers.ptr[i])

    # Free memory used by the list
    free(numbers.ptr)

    return 0
```

Because the type of `numbers.len` is `intnative` [as explained below](#how-lists-are-implemented),
it [should be printed with `%zd`](tutorial.md#intnative).

Here `List[int]{}` is [the syntax for creating a new instance of a class](classes.md#instantiating-syntax).
In this case, the class is `List[int]`, which means a list of `int`s.
Instead of `int`, you can use any other type.
For example, `List[byte*]{}` is an empty list of strings.


## What does `free(list.ptr)` do?

In any modern computer, programs can basically use two kinds of memory:
- **Stack memory** is where all local variables are stored.
    Stack variables are cleaned up automatically when a function returns.
    Also, the stack has a maximum size.
    This means that [your program crashes if you use too much stack memory](https://stackoverflow.com/questions/24904047/maximum-size-of-local-array-variable).
- **Heap memory** is used with `malloc()`, `realloc()` and `free()` (TODO: document).
    There is no maximum size:
    if you allocate a lot of heap memory, the computer usually runs out of RAM.
    There is no automatic cleanup.
    You must call the `free()` function when you're done with using a chunk of heap memory.

Lists allocate heap memory when you `.append()` items onto them,
and it is your responsibility to free that memory when you no longer need the list.

If you forget to free the memory of a list,
your program has a [memory leak](https://en.wikipedia.org/wiki/Memory_leak).
For example, the following program contains a memory leak:

```python
import "stdlib/list.jou"

def main() -> int:
    numbers = List[int]{}
    for i = 0; i < 100; i++:
        numbers.append(i)
    return 0
```

On Linux, you can use `jou --valgrind` to check for memory leaks.
Here's what `jou --valgrind` says for the above program:

```
$ jou --valgrind a.jou
==25607== 512 bytes in 1 blocks are definitely lost in loss record 1 of 1
==25607==    at 0x484682F: realloc (vg_replace_malloc.c:1437)
==25607==    by 0x1091EA: main (in /home/akuli/jou/jou_compiled/a/a)
==25607==
```

Here `realloc()` is the function that the `List` class uses to allocate heap memory.
Unfortunately, the error message says nothing about `List`,
because the `List` class uses a lot of [`@inline`](inline.md).
You can also see that the list allocated 512 bytes
even though 400 bytes would be enough for 100 ints (each `int` is 4 bytes).

See [the UB documentation](ub.md#crashing-and-valgrind) for more about `jou --valgrind`.


## How lists are implemented

The Jou compiler does not treat lists specially in any way:
as far as it can tell, `List` is just a class defined in [stdlib/list.jou](../stdlib/list.jou).
This means that if you want to learn how lists work or make your own `List` class,
you can simply look at how the `List` class is defined.

Each list has a [pointer](tutorial.md#pointers) called `ptr`
that points to heap memory used by the list.
For now, let's assume that the list items are ints.

```python
class SimpleList:
    ptr: int*
```

The list also needs to know how many items it contains.
To do that, let's add another member called `len`.
It's tempting to use `int` for this:

```python
class SimpleList:
    ptr: int*
    len: int
```

This would limit lists to [at most 2147483647 items](tutorial.md#byte-int-int64).
That would be fine for most use cases, but we can do better,
and because this is library code that will be used for many different things, we *should* do better.
We could use `int64`:

```python
class SimpleList:
    ptr: int*
    len: int64
```

An even better choice is [the `intnative` type](tutorial.md#intnative).
Many functions in [stdlib/mem.jou](../stdlib/mem.jou) expect sizes to be specified with `intnative`,
so with `len: int64`, cross-platform code would need to use a lot of `list.len as intnative`
when combining lists with other memory management things.

```python
import "stdlib/intnative.jou"

class SimpleList:
    ptr: int*
    len: intnative
```

Now, let's say we append an item to a list whose `len` is `4`.
We could allocate enough memory to hold 5 items.
But because allocating heap memory is slow,
it's better to allocate more than enough, e.g. enough for 8 items.
This way the next 3 appends don't need to allocate memory at all.
To do this, we need to keep track of how much memory we have already allocated:

```python
import "stdlib/intnative.jou"

class SimpleList:
    ptr: int*
    len: intnative
    alloc: intnative
```

That's basically all there is to it. The rest is quite straight-forward.


## Creating an empty list

Creating a list with [the `List[SomeType]{}` syntax](classes.md#instantiating-syntax)
sets `ptr` to `NULL`, `len` to zero and `alloc` to zero.
This means that you get an empty list.
You can also use [`memset()` or some other way](classes.md#instantiating-syntax)
to zero-initialize a `List` instance.


## Looping through a list

There are two commonly used ways to loop through lists in Jou.
The most straight-forward way is to use indexes:

```python
for i = 0; i < list.len; i++:
    printf("List item: %d\n", list.ptr[i])
```

If you don't like indexes, you can also use pointers.
Instead of `i = 0`, we start by getting a pointer (often named `p`) to the start of the list.
Instead of `i < list.len`, we check if `p` still points inside the list, not beyond its end.
Instead of `i++`, we can simply do `p++` to move `p` to the next list element,
because the elements are stored next to each other in heap memory (just like with arrays).
Here's how it looks:

```python
for p = list.ptr; p < list.end(); p++:
    printf("List item: %d\n", *p)
```

**Do not append to a list while looping through it with pointers.**
That creates confusing bugs.
The problem is that when more memory is allocated,
the list items may need to be moved to a new location in the memory.
When that happens, the `ptr` of the list changes,
and any pointer that was computed from the old `ptr`
will point inside the old memory location instead of the new one.

See [the documentation on loops](loops.md#looping-through-a-list-backwards)
for more tricks, such as looping through a list backwards.


## Passing lists around

If you want to make a function that adds more items to a list,
it needs to [take the list as a pointer](classes.md#pointers).
Like this:

```python
import "stdlib/io.jou"
import "stdlib/list.jou"
import "stdlib/mem.jou"

def add_one(list: List[int]*) -> None:
    list.append(1)

def main() -> int:
    numbers = List[int]{}
    add_one(&numbers)
    add_one(&numbers)
    add_one(&numbers)

    # Output: 1
    # Output: 1
    # Output: 1
    for i = 0; i < numbers.len; i++:
        printf("%d\n", numbers.ptr[i])

    free(numbers.ptr)
    return 0
```

If you do `def add_one(list: List[int])` without a pointer,
the `add_one()` function receives a copy of the `List` instance,
so the length of the list will not update inside the `main()` function.
Even worse, if the list contents need to be moved to a new memory location,
the `main()` function will see the old location of the list and probably crash the program.

However, if the length of the list won't change,
you can simply pass the list by value (that is, without a pointer):

```python
def print_items(list: List[int]) -> None:
    for i = 0; i < list.len; i++:
        printf("%d\n", list.ptr[i])

def add_one_to_all_items(list: List[int]) -> None:
    for i = 0; i < list.len; i++:
        list.ptr[i]++
```


## Accessing list items from start or end

Use `list.ptr[0]` to get the first list item, `list.ptr[1]` to get the second, `list.ptr[2]` to get the third and so on.
[The Jou tutorial explains how this syntax works](tutorial.md#more-about-strings).

The `list.end()` method is equivalent to `&list.ptr[list.len]`.
In other words, it is a pointer just beyond the end of the list.
Use `list.end()[-1]` to get the last list item, `list.end()[-2]` to get the item just before the last, and so on.


## Deleting the last list element

Use `.pop()` to delete the last element of a list:

```python
import "stdlib/io.jou"
import "stdlib/list.jou"
import "stdlib/mem.jou"  # For the free() function

def main() -> int:
    names = List[byte*]{}
    names.append("Akuli")
    names.append("littlewhitecloud")
    names.append("Moosems")

    popped = names.pop()
    printf("Popped %s\n", popped)                    # Output: Popped Moosems
    printf("%d people remain.\n", names.len as int)  # Output: 2 people remain.

    free(names.ptr)
    return 0
```

The `.pop()` method never frees allocated memory,
it only gets the last item and decrements the `len`.


## Deleting a specific list element

Use `list.ptr[i] = list.pop()` to delete an item in the middle of the list:

```python
import "stdlib/io.jou"
import "stdlib/list.jou"
import "stdlib/mem.jou"  # For the free() function

def main() -> int:
    names = List[byte*]{}
    names.append("Akuli")
    names.append("littlewhitecloud")
    names.append("Moosems")

    # Replace Akuli with Moosems
    names.ptr[0] = names.pop()

    # Output: Moosems
    # Output: littlewhitecloud
    for p = names.ptr; p < names.end(); p++:
        printf("%s\n", *p)

    free(names.ptr)
    return 0
```

Note that this affects the order of the list:
`Moosems` moved to the start of the list where `Akuli` was, before `littlewhitecloud`.

Jou's `list` does not have a method that deletes an item at a given index `i` without affecting the order of other items.
If you would find it useful, please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new).
For now, you can use the `memmove()` function declared in [stdlib/mem.jou](../stdlib/mem.jou).
It works so that `memmove(dest, src, n)` copies `n` bytes of memory starting at pointer `src`
to a new location that starts at pointer `dest`.
For example, to delete the first list item,
you can copy from the location of the second list item
to where the first list item is:

```python
import "stdlib/io.jou"
import "stdlib/list.jou"
import "stdlib/mem.jou"  # For the free() function

def main() -> int:
    names = List[byte*]{}
    names.append("Akuli")
    names.append("littlewhitecloud")
    names.append("Moosems")

    # Delete Akuli from start of list
    memmove(names.ptr, &names.ptr[1], sizeof(names.ptr[0]) * (names.len - 1))
    names.len--

    # Output: littlewhitecloud
    # Output: Moosems
    for p = names.ptr; p < names.end(); p++:
        printf("%s\n", *p)

    free(names.ptr)
    return 0
```

Here `sizeof(names.ptr[0])` is the size of one list item,
and multiplying it by `names.len - 1` means that we move all list items except one.


## Deleting all list elements

Simply set `list.len = 0`.

This does not free the allocated memory,
and the memory will be reused when items are appended to the list.
If you also want to free all allocated memory, use:

```python
free(list.ptr)
list = List[int]{}  # change int to match type of list
```


## Appending many items at once

The `.extend()` method adds all elements from another list:

```python
import "stdlib/io.jou"
import "stdlib/list.jou"
import "stdlib/mem.jou"  # For the free() function

def main() -> int:
    names1 = List[byte*]{}
    names1.append("Akuli")

    names2 = List[byte*]{}
    names2.append("littlewhitecloud")
    names2.append("Moosems")

    names1.extend(names2)
    free(names2.ptr)

    # Output: Akuli
    # Output: littlewhitecloud
    # Output: Moosems
    for p = names1.ptr; p < names1.end(); p++:
        printf("%s\n", *p)

    free(names1.ptr)
    return 0
```

Do not extend a list with itself, as in `names1.extend(names1)`.
This fails if the list items are moved to a different memory location,
because the argument of `extend()` is passed by value,
and its `ptr` is the old memory location
(see [above](#passing-lists-around)).
[Create an issue on GitHub](https://github.com/Akuli/jou/issues/new)
if you think it would be a good idea to support extending a list with itself.

TODO: document `extend_from_ptr()`
