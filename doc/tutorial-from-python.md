# Jou tutorial for Python programmers

**TODO:** write another tutorial for people coming from C

This is a tutorial for people who have mostly used high-level programming languages,
such as Python or JavaScript.

On surface level, Jou looks a lot like Python, but **it doesn't behave like Python**,
so you will probably be disappointed if you expect all of your existing Python knowledge to work as is.
The main differences are:
- Jou is compiled into native binaries, not interpreted.
- Jou uses C's standard library.
- Jou integer types are fixed-size and can wrap around.
- All data in a computer consists of bytes. High-level languages hide this fact, Jou exposes it.
- Jou is not memory safe.
- Jou doesn't hide various other details about how your computer works.
- Jou uses manual memory management, not garbage-collection.

If none of this makes any sense to you, that's fine.
I will explain it all shortly using lots of example code.

Basically, all of this means that Jou is more difficult to use, but as a result,
Jou code tends to run faster than e.g. Python (see [the performance docs](./perf.md) for more details).
Also, knowing Jou makes learning other low-level languages (Rust, C, C++, ...) much easier.


## Main function and binaries

When you run a Jou program, Jou first produces an executable file and then runs it.
On Windows, executable file names must end with `.exe` (e.g. `jou.exe` or `hello.exe`).
On most other systems, executable files typically don't have a file extension at all (e.g. `jou` or `hello`).
The executable is placed into a folder named `jou_compiled/` to keep things nice and organized.

For example, if you run `hello.jou`, you get a file named
`jou_compiled\hello\hello.exe` (Windows) or `jou_compiled/hello/hello` (other platforms).
You can run this file without Jou, or even move it to a different computer that doesn't have Jou, and run it there.

When the operating system runs an executable,
it finds a function named `main()` in it and calls it.
The return value of the `main()` function is an integer,
and the operating system gives it to the program that ran the executable.
This means that every executable must have a `main()` function that returns an integer.
Jou doesn't hide this, and therefore most Jou programs contain something like this:

```python
def main() -> int:
    ...
    return 0
```

By convention, the return value `0` means "success". Anything else means "error".
You can use different values to represent different errors, but `1` is the most common.


## Printing

Jou does not have a `print()` function that can magically print a value of any type.
The closest equivalent is `printf()` from [stdlib/io.jou](../stdlib/io.jou).
Here's how it works:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("Hello\n")  # Output: Hello
    printf("strings %s %s\n", "foo", "bar")  # Output: strings foo bar
    printf("ints %d %d\n", 1, 2, 3)  # Output: ints 1 2 3
    printf("doubles %f %.2f\n", 3.1415, 3.1415)  # Output: doubles 3.141500 3.14
    printf("doubles %f %.2f\n", 3.1415 as float, 3.1415 as float)  # Output: floats 3.141500 3.14
    printf("%d is %s and %d is %s\n", 4, "even", 7, "odd")  # Output: 4 is even and 7 is odd
    return 0
```

Here:
- `%d` gets replaced with an `int` argument that you provide
- `%s` means a string
- `%f` means a `float` or `double` (`float` takes up less memory but is also less accurate, just use `double` if you don't know which to use)
- `%.2f` means a `float` or `double` rounded to two decimal places
- `as float` is a type cast, needed to construct a `float`.

There are various other format specifiers you can pass to `printf()`.
Just search something like "printf format specifiers" online:
`printf()` is actually not a Jou-specific thing (see below).

You need the `\n` to get a newline.
The `printf()` function doesn't add it automatically.
This seems annoying, but on the other hand, it means that you can do things like this:

```python
# Output: the numbers are 1 2 3
printf("the numbers are")
for i = 1; i <= 3; i++:
    printf(" %d", i)
printf("\n")
```


## C's standard library (libc)

We did `import "stdlib/io.jou"` to use the `printf()` function.
If you look at [stdlib/io.jou](../stdlib/io.jou), the definition of `printf()` is only one line:

```python
declare printf(pattern: byte*, ...) -> int  # Example: printf("%s %d\n", "hi", 123)
```

How in the world can this one line of code define a function that does so many different things?

This doesn't actually define the `printf()` function, it only **declares** it.
This line of code tells the Jou compiler
"there exists a function named `printf()`, and it is defined somewhere else".
The `printf()` function is actually defined in the **libc**,
which is the standard library of the C programming language.

C is an old, small, simple and low-level programming language,
and most newer languages use many things that first appeared in C
(`if` statements, `while` loops, `for` loops, `break`, `continue` to name a few).

Jou is compatible with C, which basically means that you can use C libraries in Jou code.
This means that any large Jou project will depend on a libc anyway,
so we might as well use things that the libc provides instead of reinventing the wheel.


## `byte`, `int`, `long`

From a programmer's point of view, a byte is a number from 0 to 255 (inclusive).
In Jou, the `byte` data type represents a single byte.
To construct a byte, you can do e.g. `123 as byte`,
where the type cast with `as` converts from `int` to `byte`.
If you try to convert a number larger than 255 into a `byte`, it will wrap back around to zero:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d %d %d %d\n", 254 as byte)  # Output: 254
    printf("%d %d %d %d\n", 255 as byte)  # Output: 255
    printf("%d %d %d %d\n", 256 as byte)  # Output: 0
    printf("%d %d %d %d\n", 257 as byte)  # Output: 1
    return 0
```

Bytes get converted to `int` implicitly when calling `printf()`,
so it's fine to specify `%d` and pass in a `byte`.

Each byte has 256 different possible values (0 - 255),
so with 2 bytes, you get `256 * 256` different values:
for each first byte, you have 256 possible second bytes.
Similarly, with 4 bytes (Jou `int`) you have `256 * 256 * 256 * 256 = 4294967296` different combinations.
Half of these combinations are used for negative numbers, and one of them is used for zero,
so the biggest possible `int` value is `2147483647`.
You will get a compiler error if your code contains an `int` larger than that:

```python
printf("%d\n", 2147483648)  # Error: value does not fit in a signed 32-bit integer
```

However, math operations will wrap around if the result does not fit into an `int`.
Typically this results in something being negative when you expect it to be positive.
If this becomes a problem, you can use `long` instead of `int`.
To create a `long`, you need to put `L` to the end of the number.
To print a `long`, use `%lld` instead of `%d`.

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 1000 * 1000 * 1000 * 1000)        # Output: -727379968
    printf("%lld\n", 1000L * 1000L * 1000L * 1000L)  # Output: 1000000000000
    return 0
```

A Jou `long` uses 8 bytes, so the biggest value is `9223372036854775807`.
If that isn't big enough for your use case, please create an issue on GitHub.


## Pointers

In this context, "memory" means the computer's RAM, not hard disk or SSD.

All data in any modern computer consists of bytes.
A computer's memory is basically a big list of bytes,
and an `int` is just 4 consecutive bytes somewhere inside that list.
Jou does not hide that, and in fact, as a Jou programmer
**you will need to think about this a lot**.

To get started, let's make a variable and ask Jou to print its index in the big list of bytes:

```python
import "stdlib/io.jou"

def main() -> int:
    b = 123 as byte
    printf("%p\n", &b)
    return 0
```

This prints something like `0x7ffd85fd3db7`.
This is a number written in hexadecimal,
and it means `140726851419575`.
How exactly hexadecimal works is not really relevant here,
but what matters is that we got some number.
So:

```
memory_of_the_computer[140726851419575] == 123
```

Numbers that represent indexes into the computer's memory like this
are called **memory addresses**.
The `&` operator is called the **address-of operator**,
because `&b` computes the address of the `b` variable.

If you run the code above,
you will almost certainly get a different memory address than I got.
Even on the same computer I get a different memory address every time,
because the program essentially loads into whatever memory location is available:

```
$ ./jou asd.jou
0x7ffe7e1ded17
$ ./jou asd.jou
0x7ffff24bec87
$ ./jou asd.jou
0x7fff356b6dd7
$ ./jou asd.jou
0x7ffeabcfe7f7
```

<p><details>
<summary>An unimportant side note that you can skip</summary>

The memory addresses visible to the program are not necessary just indexes into RAM.
For example, the Linux kernel moves infrequently accessed things to disk
when RAM is about to get full (this is called **swapping**).
This doesn't change memory addresses within the program whose memory is moved to disk,
so you don't need to think about swapping when you write Jou programs.
The OS will take care of mapping your memory addresses to the right place.

I believe the locations in RAM are called **physical addresses**,
and the memory addresses that Jou programs see are called **virtual addresses**.
I'm not sure about the names though.
I don't think of this much: I just imagine that everything goes in RAM,
and on the rest of this page I continue doing so.

</details>

In Jou, memory addresses are represented as **pointers**.
A pointer is a memory address together with a type.
For example, `&b` is a pointer of type `byte*`, meaning a pointer to a value of type `byte`.
We could, for example, make a function that sets the value of a given `byte*`:

```python
import "stdlib/io.jou"

def set_to_50(pointer: byte*) -> void:
    *pointer = 50

def main() -> int:
    b = 123 as byte
    set_to_50(&b)
    printf("%d\n", b)  # Output: 50
    return 0
```

Because the `set_to_50()` function knows the memory address of the `b` variable,
it can just set the value at that memory address.
The `*` operator is sometimes called the **value-of operator**,
and `*foo` means the value of a pointer `foo`.

This means that a function call can change the values of variables outside that function.
However, the variables are clearly marked with `&`, so after getting used to this,
it isn't as confusing as it seems.
A common way to use this is to return multiple values from the same function:

```python
import "stdlib/io.jou"

def get_point(x: int*, y: int*) -> void:
    *x = 123
    *y = 456

def main() -> int:
    x: int
    y: int
    get_point(&x, &y)
    printf("The point is (%d,%d)\n", x, y)  # Output: The point is (123,456)
    return 0
```

Here `x: int` creates a variable of type `int` without assigning a value to it.
If you try to use the value of `x` before it is set (by calling `get_point(&x, &y)` for example),
you will most likely get a compiler warning together with a random garbage value when the program runs.
For example, if I delete the `get_point(&x, &y)` line, I get:

```
compiler warning for file "asd.jou", line 10: the value of 'x' is undefined
compiler warning for file "asd.jou", line 10: the value of 'y' is undefined
The point is (-126484104,-126484088)
```

Again, Jou doesn't attempt to hide the way the computer's memory works.
When you do `x: int`, you tell Jou: "give me 4 bytes of memory and interpret it as an integer".
That memory has probably been used for something else before your function gets it,
so it will contain whatever the previous thing had there previously.
Once it is interpreted as an integer, you tend to get something nonsensical.
Also, the Jou compiler **does not always warn you** when this happens,
because it is difficult to detect this in more complicated cases.

**Jou is not a memory safe language.**
It basically means that you can do dumb things like this with the computer's memory,
and Jou will let you do it (though it will often warn you).

As a side note, you could also use an `int[2]` array to return the two values.
This is simpler, but doesn't work if the return values are of different types.

```python
import "stdlib/io.jou"

def get_point() -> int[2]:
    return [123, 456]

def main() -> int:
    point = get_point()
    printf("The point is (%d,%d)\n", point[0], point[1])  # Output: The point is (123,456)
    return 0
```


## Memory safety, speed, ease of use: pick two

Ideally, a programming language would be:
- memory safe
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


## Strings and zero bytes

A Jou string is just a chunk of memory,
represented as a `byte*` pointer to the start of the memory.
There is a `0` byte to mark the end of the string.

For example, the string `"hello"` is 6 bytes. Let's print the bytes.

```python
import "stdlib/io.jou"

def main() -> int:
    s = "hello"
    for i = 0; i < 6; i++:
        printf("byte %d = %d\n", s[i])
    return 0

# Output: byte 0 = 104
# Output: byte 1 = 101
# Output: byte 2 = 108
# Output: byte 3 = 108
# Output: byte 4 = 111
# Output: byte 5 = 0
```

Each number corresponds with a letter. For example, 108 is the letter `l`.
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

You can also use the `++` operator to move a pointer forward by one item:

```python
import "stdlib/io.jou"

def main() -> int:
    s = "hello"
    s++
    printf("%s\n", s)  # Output: ello
    s++
    printf("%s\n", s)  # Output: llo
    return 0
```

To instead remove characters from the end of the string,
you can simply place a zero byte to the middle of the string.
Usually the zero byte is written as `'\0'`,
because after getting used to it, that is more readable than `0 as byte`.
Note that single quotes are used for individual bytes
and double quotes are used for strings.

```python
import "stdlib/io.jou"

def main() -> int:
    s = "hello"
    s[2] = '\0'
    printf("%s\n", s)  # Output: he
    return 0
```

However, this introduces a subtle bug.
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
When the loop does `s = "hello"` for a second time, it gets the truncated version `"he"`.

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
If we write all implicit casts with **explicit casts** (that is, with the `as` syntax),
the above example becomes:

```python
import "stdlib/io.jou"

def main() -> int:
    for i = 0; i < 3; i++:
        s: byte[100] = "hello"
        printf("Before truncation: %s\n", s as byte*)
        (s as byte*)[2] = '\0'
        printf("After truncation: %s\n", s as byte*)
    return 0
```



TODO: note about 100
TODO: note about going out of bounds + segfaults
TODO: strlen and other functions



## Motivation

Before we dive into details, why should you even care about Jou if you already know Python?

I believe every good programming language has **two** of the following:
- Good performance (programs run fast)
- Memory safety (
