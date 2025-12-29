# Jou Tutorial

On surface level, Jou looks a lot like Python, but **it doesn't behave like Python**,
so you will probably be disappointed if you know Python well and you expect all of your knowledge to work as is.
The main differences are:
- Jou is compiled into native binaries, not interpreted.
- Jou uses C's standard library.
- Jou's integer types are fixed-size and can wrap around.
- All data in a computer consists of bytes. High-level languages hide this fact, Jou exposes it.
- Jou doesn't hide various other details about how computers work.
- Jou has Undefined Behavior.
- Jou uses manual memory management, not garbage-collection.

If none of this makes any sense to you, that's fine.
The rest of this page explains it all using lots of example code.

Basically, all of this means that Jou is more difficult to use, but as a result,
Jou code tends to run faster than e.g. Python (see [the performance docs](./perf.md) for more details).
Also, knowing Jou makes learning other low-level languages (C, C++, Rust, ...) much easier.


## Main function and binaries

When you run a Jou program, Jou first produces an executable file, and then runs it.
On Windows, executable file names must end with `.exe` (e.g. `jou.exe` or `hello.exe`).
On most other systems, executable files typically don't have a file extension at all (e.g. `jou` or `hello`).
By default, Jou places executables into a folder named `jou_compiled/`.

For example, if you run `hello.jou`, you get a file named
`jou_compiled\hello\hello.exe` (Windows) or `jou_compiled/hello/hello` (other platforms).
You can run this file without Jou, or even move it to a different computer that doesn't have Jou, and run it there.

When the operating system runs an executable,
it finds a function named `main()` in it and calls it.
The return value of the `main()` function is an integer,
and the operating system gives it to the program that ran the executable.
This means that every executable must have a `main()` function that returns an integer.
Jou doesn't hide this, and therefore all Jou programs contain something like this:

```python
def main() -> int:
    ...
    return 0
```

This integer is called the **exit code** of the process.
By convention, exit code `0` means "success". Anything else means "error".
You can use different exit codes to represent different errors, but `1` is the most common.


## Printing

To print a string, you can use the `puts()` function from [stdlib/io.jou](../stdlib/io.jou):

```python
import "stdlib/io.jou"

def main() -> int:
    puts("Hello")  # Output: Hello
    return 0
```

However, `puts()` only prints strings.
You can use `printf()` to print values of other types.
Here's how it works:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("Hello\n")  # Output: Hello
    printf("strings %s %s\n", "foo", "bar")  # Output: strings foo bar
    printf("ints %d %d %d\n", 1, 2, 3)  # Output: ints 1 2 3
    printf("doubles %f %.2f\n", 3.1415, 3.1415)  # Output: doubles 3.141500 3.14
    printf("floats %f %.2f\n", 3.1415 as float, 3.1415 as float)  # Output: floats 3.141500 3.14
    printf("%d is %s and %d is %s\n", 4, "even", 7, "odd")  # Output: 4 is even and 7 is odd
    return 0
```

Here:
- `%d` gets replaced with an `int` argument that you provide
- `%s` means a string
- `%f` means a `float` or `double` (`float` takes up less memory but is also less accurate, just use `double` if you don't know which to use)
- `%.2f` means a `float` or `double` rounded to two decimal places
- `as float` is a type cast, needed to construct a `float`.

There are various other `%` things you can pass to `printf()`.
Just search something like "printf format specifiers" online:
`printf()` is actually not a Jou-specific thing (see below).

You need the `\n` to get a newline.
The `printf()` function doesn't add it automatically.
This seems annoying, but on the other hand, it means that you can do things like this:

```python
import "stdlib/io.jou"

# Output: the numbers are 1 2 3
def main() -> int:
    printf("the numbers are")
    for i = 1; i <= 3; i++:
        printf(" %d", i)
    printf("\n")
    return 0
```


## C's standard library (libc)

We did `import "stdlib/io.jou"` to use the `printf()` function.
If you look at [stdlib/io.jou](../stdlib/io.jou),
there is only one line of code related to `printf()`:

```python
@public
declare printf(pattern: byte*, ...) -> int  # Example: printf("%s %d\n", "hi", 123)
```

Here the `@public` decorator means that it is possible to import `printf` into another file.
The other line is more interesting:
how in the world can one line of code define a function that does so many different things?

This doesn't actually define the `printf()` function, it only **declares** it.
This line of code tells the Jou compiler
"there exists a function named `printf()`, and it is defined somewhere else".
The `printf()` function is actually defined in the **libc**,
which is the standard library of the C programming language.

C is an old, small, simple and low-level programming language.
Jou is very heavily inspired by C, and in many ways similar to C and compatible with C.
For example, Jou programs can use libraries written in C,
so in practice, any large Jou project needs libc anyway.
With `declare`, we basically use things that the libc provides instead of reinventing the wheel.


## `byte`, `int`, `int64`

From a programmer's point of view, a byte is an integer between 0 and 255 (inclusive).
Alternatively, you can think of a `byte` as consisting of 8 bits, where a bit means 0 or 1.
Two bits can be set to 4 different states (00, 01, 10, 11), so you could use 2 bits to represent numbers 0 to 3.
Similarly, 8 bits can be set to 256 different states
that correspond with numbers 0 to 255.

In Jou, the `byte` data type represents a single byte.
To construct a byte, you can do e.g. `123 as byte`,
where the type cast with `as` converts from `int` to `byte`.
If you try to convert a number larger than 255 into a `byte`, it will wrap back around to zero:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 254 as byte)  # Output: 254
    printf("%d\n", 255 as byte)  # Output: 255
    printf("%d\n", 256 as byte)  # Output: 0
    printf("%d\n", 257 as byte)  # Output: 1
    printf("%d\n", 258 as byte)  # Output: 2
    return 0
```

Bytes get converted to `int` implicitly when calling `printf()`,
so it's fine to specify `%d` and pass in a `byte`.

If the compiler expects the value to be a `byte`, you don't need to use `as`.
For example, you can create a variable and specify its type as `byte`:

```python
import "stdlib/io.jou"

def main() -> int:
    b: byte = 255
    printf("%d\n", b)  # Output: 255
    return 0
```

If you don't use `as`, the value will not wrap around, and you will instead get a compiler error:

```python
import "stdlib/io.jou"

def main() -> int:
    b: byte = 1234  # Error: value does not fit into byte
    return 0
```

Each byte has 256 different possible values (0 - 255),
so with 2 bytes, you get `256 * 256` different values:
for each first byte, you have 256 possible second bytes.
If we used 4 bytes instead of one byte, we would get `256 * 256 * 256 * 256 = 4294967296` different combinations,
and we would be able to handle much bigger numbers.
In fact, this is exactly what Jou's `int` does:
**Jou's `int` is 4 bytes (32 bits)**.
For example, `1000` and `1000000` are valid `int`s:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 1000 * 1000)         # Output: 1000000
    printf("%d\n", 1000 * 1000 * 1000)  # Output: 1000000000
    return 0
```

Specifically, the range of an `int` is from `-2147483648` to `2147483647`.
Note that `int`s can be negative, but bytes cannot.
This works by basically using the first bit as the sign bit:
the first bit is 1 for negative numbers and 0 for nonnegative numbers,
and the remaining 31 bits work more or less like you would expect.

Sometimes `int` isn't big enough.
When `int` wraps around, you usually get negative numbers when you expect things to be positive,
and you should probably use [`int64`](types.md#integers) instead of `int`.
Jou's `int64` is 8 bytes (64 bits), so twice the size of an `int` and hence much less likely to wrap around.
You can use `as` to create an `int64`.
To print an `int64`, use `%lld` instead of `%d`.

```python
import "stdlib/io.jou"

def main() -> int:
    # Output: 123123123123123 is a big number
    printf("%lld is a big number\n", 123123123123123 as int64)

    # Output: -727379968
    printf("%d\n", 1000 * 1000 * 1000 * 1000)

    # Output: 1000000000000
    printf("%lld\n", (1000 as int64) * (1000 as int64) * (1000 as int64) * (1000 as int64))

    return 0
```

The range of `int64` is from `-9223372036854775808` to `9223372036854775807`.
Please create an issue on GitHub if you need an even larger range.


## Pointers

In this context, "memory" means the computer's RAM, not hard disk or SSD.

All data in any modern computer consists of bytes.
A computer's memory is basically a big list of bytes,
and an `int` is just 4 consecutive bytes somewhere inside that list.
Jou does not hide that, and in fact, as a Jou programmer
**you will need to often treat the computer's memory as a big array of bytes**.

To get started, let's make a variable and ask Jou to print its index in the big list of bytes:

```python
import "stdlib/io.jou"

def main() -> int:
    b = 123 as byte
    printf("%lld\n", &b as int64)
    return 0
```

This prints something like `140726851419575`, so:

```
memory_of_the_computer[140726851419575] == 123
```

Numbers that represent indexes into the computer's memory like this
are called **memory addresses**.
The `&` operator is called the **address-of operator**,
because `&b` computes the address of the `b` variable.

<p><details>
<summary>An unimportant "ahchthually" that you can skip</summary>

The memory addresses are not necessary just indexes into RAM.
For example, the Linux kernel moves infrequently accessed things to disk
when RAM is about to get full (this is called **swapping**).
This doesn't change memory addresses within the program,
so you don't need to think about swapping when you write Jou programs.
The OS will take care of mapping your memory addresses to the right place.

I think the locations in RAM are called **physical addresses**,
and the memory addresses that Jou programs see are called **virtual addresses**.
I'm not sure about the names though.
I don't think of this much: I just imagine that everything goes in RAM,
and on the rest of this page I continue to do so.

</details>

If you run the code above,
you will almost certainly get a different memory address than I got.
Even on the same computer I get a different memory address every time,
because the program essentially loads into whatever memory location is available:

```
$ ./jou asd.jou
140731014311191
$ ./jou asd.jou
140737258450055
$ ./jou asd.jou
140734089620951
$ ./jou asd.jou
140731780950007
```

In Jou, memory addresses are represented as **pointers**.
A pointer is a memory address together with a type.
For example, `&b` is a pointer of type `byte*`, meaning a pointer to a value of type `byte`.
Similarly, `int*` would be a pointer to a value of type `int`,
pointing to the first of the 4 consecutive bytes that an `int` uses.
We could, for example, make a function that sets the value of a given `int*`:

```python
import "stdlib/io.jou"

def set_to_500(pointer: int*) -> None:
    *pointer = 500

def main() -> int:
    n = 123
    set_to_500(&n)
    printf("%d\n", n)  # Output: 500
    return 0
```

Because the `set_to_500()` function knows the memory address of the `n` variable,
it can just set the value at that memory address.
The `*` operator is sometimes called the **value-of operator**,
and `*foo` means the value of a pointer `foo`.
Note that the value-of operator is the opposite of the address-of operator:
`&*foo` and `*&foo` are unnecessary, because you might as well use `foo` directly.

As you can see, a function call can change the values of variables outside that function.
However, the variables passed as pointers are clearly marked with `&`,
so it isn't as confusing as it seems to be at first.
A common way to use this is to return multiple values from the same function:

```python
import "stdlib/io.jou"

def get_point(x: int*, y: int*) -> None:
    *x = 123
    *y = 456

def main() -> int:
    x, y: int
    get_point(&x, &y)
    printf("The point is (%d,%d)\n", x, y)  # Output: The point is (123,456)
    return 0
```

Here `x, y: int` creates two integer variables without assigning values to them.
This means that we leave 8 bytes (4 bytes for both) of the computer's memory unused for now.
We then pass the location of that memory to `get_point()`,
so that it can write to that memory, i.e. set the values of the `x` and `y` variables.

Instead of pointers, you could also use an `int[2]` array to return the two values:

```python
import "stdlib/io.jou"

def get_point() -> int[2]:
    return [123, 456]

def main() -> int:
    point = get_point()
    printf("The point is (%d,%d)\n", point[0], point[1])  # Output: The point is (123,456)
    return 0
```

However, **this doesn't mean that you don't need to understand pointers**,
as they have many other uses in Jou.
Pointers are used for strings, arrays that are not fixed-size,
instances of most classes, and so on.

Basically, you need pointers whenever you want to use a large object in multiple places
without making several copies of it.
Instead, you just make one object and point to it from many places.
This is probably what you expect if you have mostly used high-level languages,
like Python or JavaScript.
In fact, in Python, **all** objects are passed around as pointers.

You usually don't need pointers for small objects.
For example, if you want to make a function takes two `int`s and prints them,
just make a function that takes two `int`s.
On the other hand, if your function takes an array of 100000 `int`s,
you should use a pointer.
Passing around hundreds or thousands of bytes without pointers is usually a bad idea.


## Undefined Behavior (UB)

Consider again the pointer example above:

```python
import "stdlib/io.jou"

def get_point(x: int*, y: int*) -> None:
    *x = 123
    *y = 456

def main() -> int:
    x, y: int
    get_point(&x, &y)
    printf("The point is (%d,%d)\n", x, y)  # Output: The point is (123,456)
    return 0
```

Here `x, y: int` creates two variables of type `int` without assigning values to them.
If you try to use the value of `x` or `y` before they are set,
you will most likely get a compiler warning together with a random garbage value when the program runs.
For example, if I delete the `get_point(&x, &y)` line, I get:

```
compiler warning for file "asd.jou", line 10: the value of 'x' is undefined
compiler warning for file "asd.jou", line 10: the value of 'y' is undefined
The point is (-126484104,-126484088)
```

Again, Jou doesn't attempt to hide the way the computer's memory works.
When you do `x: int`, you tell Jou:
"give me 4 bytes of memory, and from now on, interpret those 4 bytes as an integer".
That memory has probably been used for something else before your function gets it,
so it will contain whatever the previous thing stored there.
Those 4 bytes were probably not used as an integer,
and once you interpret them as an integer anyway,
you tend to get something nonsensical.

This is one example of **UB (Undefined Behavior)** in Jou.
In general, UB is a Bad Thing, because code that contains UB can behave unpredictably.
You need to know about UB,
because **the Jou compiler does not always warn you when you're about to do UB.**
See [UB documentation](ub.md) for more info.


## Memory safety, speed, ease of use: pick two

Ideally, a programming language would be:
- memory safe (basically means that you cannot get UB by accident)
- fast
- simple/easy to use.

So far I haven't seen a programming language that would check all boxes to me,
and I think it is not possible to make such a language.
However, every combination of two features has been done:
- Jou and C are fast and simple languages, but not memory safe.
- Python is memory safe and easy to use, but not very fast compared to Jou or C.
- Rust is memory safe and fast, but difficult to use.

Jou intentionally chooses the same tradeoff as C.
The purpose of Jou is to be a lot like C,
but with various annoyances fixed, and of course, with Python's simple syntax.


## 32-bit vs 64-bit

As you probably know, most computers are 64-bit.
It basically means that the size of a [pointer](#pointers) is 64 bits,
and in general, the computer is good at doing math with 64-bit numbers.

The following program prints the size of a pointer.
On a 64-bit computer, it prints `8 bytes`,
because [the `sizeof` keyword](keywords.md#sizeof) gives the size as bytes,
and 8 bytes is 64 bits because each byte is 8 bits.

```python
import "stdlib/io.jou"

def main() -> int:
    thing = 123
    printf("%d bytes\n", sizeof(&thing))
    return 0
```

Jou also supports some 32-bit platforms (TODO: link to list of supported platforms when it exists),
so on some computers the above program prints `4` instead of `8`.

You can use the `int64` and `uint64` types even on 32-bit systems.
This is always possible, because the compiler can use two 32-bit numbers to fake a 64-bit number:

![A funny image that illustrates how 64-bit numbers work on 32-bit computers](images/64bit-meme-small.jpg)


## `intnative`

Many things in [C's standard library](#cs-standard-library-libc)
are `int` on 32-bit systems and `int64` on 64-bit systems.
To support these, [stdlib/intnative.jou](../stdlib/intnative.jou)
defines the `intnative` data type using [typedef](keywords.md#typedef) like this:

```python
if IS_32BIT:
    @public
    typedef intnative = int
else:
    @public
    typedef intnative = int64
```

So `intnative` is same as `int` on 32-bit systems, and same as `int64` on 64-bit systems.
If you don't care about supporting 32-bit systems,
just think of `intnative` as another name for `int64`.
Similarly, if your code only needs to run on 32-bit systems,
think of `intnative` as another name for `int`.

You can use `as int` or `as int64` to convert an `intnative`
to a number whose size is always the same, but that is rarely needed.
For example, to print an `intnative` with `printf()`, you can use `%zd`,
which is just like `%d` on 32-bit systems,
and just like [`%lld`](#byte-int-int64) on 64-bit systems:

```python
import "stdlib/io.jou"
import "stdlib/intnative.jou"

def main() -> int:
    n = 123 as intnative

    # This always works
    printf("%zd\n", n)  # Output: 123

    # This works on 32-bit systems, but may print a garbage value on 64-bit systems
    #printf("%d\n", n)

    # This works on 64-bit systems, but may print a garbage value on 32-bit systems
    #printf("%lld\n", n)

    return 0
```


## Characters

You can place a character in single quotes to specify a byte.
This byte is the number that represents the character in the computer's memory.
For example, almost all `a` characters in your computer are represented with the byte 97.

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 'a')  # Output: 97
    printf("%d\n", ':')  # Output: 58
    printf("%d\n", '0')  # Output: 48
    return 0
```

Note that single quotes specify a byte and double quotes specify a string.

This clearly cannot work for all characters,
because there are thousands of different characters, but only 256 different bytes.
For example, `'Ω'` doesn't work:

```python
printf("%d\n", 'Ω')  # Error: single quotes are for specifying a byte, maybe use double quotes to instead make a string?
```

In fact, this only works for ASCII characters, such as letters `A-Z a-z` and numbers `0-9`.
There are a total of 128 ASCII characters (bytes 0 to 127).
Other characters are made up by combining multiple bytes per character (bytes 128 to 255).
This is how UTF-8 works.
It is used in Jou, because it is by far the most common way to represent text in computers,
and using anything else would be weird and impractical.

To see how many bytes a character consists of,
you can use the `strlen()` function from [stdlib/str.jou](../stdlib/str.jou).
It calculates the length of a string in bytes.

```python
import "stdlib/io.jou"
import "stdlib/str.jou"

def main() -> int:
    printf("%zd\n", strlen("o"))  # Output: 1
    printf("%zd\n", strlen("Ω"))  # Output: 2
    printf("%zd\n", strlen("foo"))  # Output: 3
    printf("%zd\n", strlen("fΩΩ"))  # Output: 5
    return 0
```

We are using `%zd`, because `strlen()` returns an [intnative](#intnative).
You can see it by looking at how [stdlib/str.jou](../stdlib/str.jou) declares `strlen()`:

```python
declare strlen(s: byte*) -> intnative
```


## More about strings

A Jou string is just a chunk of memory,
represented as a `byte*` pointer to the start of the memory.
There is a zero byte to mark the end of the string.

For example, the string `"hello"` is 6 bytes. Let's print the bytes.

```python
import "stdlib/io.jou"

def main() -> int:
    s = "hello"
    for i = 0; i < 6; i++:
        printf("byte %d = %d\n", i, s[i])
    return 0

# Output: byte 0 = 104
# Output: byte 1 = 101
# Output: byte 2 = 108
# Output: byte 3 = 108
# Output: byte 4 = 111
# Output: byte 5 = 0
```

Each byte corresponds with a letter. For example, 108 is the letter `l`.
You can see that it is repeated: there are two `l`'s in `hello`.

```
                                 'h'   'e'   'l'   'l'   'o'
memory_of_the_computer = [ ...,  104,  101,  108,  108,  111,  0,  ... ]
                                  ↑
                                  s
```

The syntax `s[i]` gets the value `i` items forward from the pointer.
Because we have a `byte*` pointer, each item is 1 byte,
so `s[3]` moves 3 bytes forward, for example.

```
                                'h'   'e'   'l'   'l'   'o'
memory_of_the_computer = [ ..., 104,  101,  108,  108,  111,  0,  ... ]
                                s[0]  s[1]  s[2]  s[3]  s[4] s[5]
```

To slice the string to get just `llo`, you can simply do `&s[2]`;
that is, take a pointer to `s[2]`.

```python
import "stdlib/io.jou"

def main() -> int:
    s = "hello"
    printf("%s\n", &s[2])  # Output: llo
    return 0
```

You can also use the `++` and `--` operator to move pointers by one item at a time.
They move strings one byte at a time, because strings are `byte*` pointers.

```python
import "stdlib/io.jou"

def main() -> int:
    s = "hello"
    s++
    printf("%s\n", s)  # Output: ello
    s++
    printf("%s\n", s)  # Output: llo
    s--
    s--
    printf("%s\n", s)  # Output: hello
    return 0
```

To instead remove characters from the end of the string,
you can simply place a zero byte to the middle of the string.
Usually the zero byte is written as `'\0'`, which means same as `0 as byte`
but is slightly more readable after getting used to it.

```python
import "stdlib/io.jou"

def main() -> int:
    s = "hello"
    s[2] = '\0'
    printf("%s\n", s)  # Output: he
    return 0
```

However, this code contains a subtle bug.
To see it, let's put this code into a loop and add some prints:

```python
import "stdlib/io.jou"

def main() -> int:
    for i = 0; i < 3; i++:
        s = "hello"
        printf("Before truncation: %s\n", s)
        s[2] = '\0'
        printf("After truncation: %s\n", s)
    return 0
```

This prints:

```
Before truncation: hello
After truncation: he
Before truncation: he
After truncation: he
Before truncation: he
After truncation: he
```

It seems that the string `"hello"` became permanently truncated.
When the loop does `s = "hello"` for a second time,
it actually gets the truncated version `"he"`.

**Do not modify strings in this way.**
They are not meant to be modified.
If you want to modify a string, use an array of bytes,
e.g. `byte[100]` for a maximum length of 100 bytes (including `'\0'`).
To do that, simply specify the type of the string as `byte[100]`:

```python
import "stdlib/io.jou"

def main() -> int:
    for i = 0; i < 3; i++:
        # create an array to hold the characters
        s: byte[100] = "hello"
        printf("Before truncation: %s\n", s)
        s[2] = '\0'
        printf("After truncation: %s\n", s)
    return 0
```

Now this prints:

```
Before truncation: hello
After truncation: he
Before truncation: hello
After truncation: he
Before truncation: hello
After truncation: he
```

Note that `s[2] = '\0'` and printing `s` work in the same exact way
regardless of whether `s` is a `byte*` or a `byte[100]`.
Specifically, Jou does an **implicit cast** that
takes the pointer to the first element of the array,
and so the `byte[100]` can act as a `byte*` when needed.

If you don't want to hard-code a maximum size for the string (100 in this example),
you can instead use heap memory.
The `strdup()` function from [stdlib/str.jou](../stdlib/str.jou)
allocates the right amount of heap memory to hold a string (including the `'\0'`) and copies it there.
You should `free()` the memory once you no longer need the string.

TODO: document heap allocations better

```python
import "stdlib/io.jou"
import "stdlib/str.jou"
import "stdlib/mem.jou"

def main() -> int:
    s = strdup("hello")

    printf("Before truncation: %s\n", s)  # Output: Before truncation: hello
    s[2] = '\0'
    printf("After truncation: %s\n", s)   # Output: After truncation: he

    free(s)
    return 0
```


## What next?

To learn more about Jou, I recommend:
- reading other documentation files in the [doc](../doc/) folder
- reading files in [stdlib/](../stdlib/) and [examples/](../examples/)
- writing small Jou programs (e.g. [Advent of Code](https://adventofcode.com/))
- browsing Jou's issues on GitHub and fixing some of them :)
