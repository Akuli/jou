# Undefined Behavior

Undefined behavior (UB) basically means that your code does something dumb.
For example, these things are UB:
- Reading the value of a `NULL` pointer.
- Setting the value of a `NULL` pointer.
- Reading or setting the 11th member from an array of length 10.
- Reading or setting the value of a pointer into a local variable that no longer exists.
    Local variables no longer exists after a function `return`s
    or completes by running to the end without a `return`.
- Using the value of a variable before it has been set.
    For example, `x: int` followed by `printf("%d\n", x)`
    without doing something like `x = 0` before printing.

In the rest of this file, we look at some of the most common symptoms of UB,
so that you will know what to look for when you cause UB.
Every experienced Jou (or C or C++) programmer has caused UB by accident and fixed it many times.

If your program has UB, you might get:
- a garbage value that just happened to be in the computer's memory
- random results, e.g. sometimes what you expect and sometimes a garbage value
- a perfectly working program
- a crash
- something else.

UB is not just a Jou thing.
If you want to learn other "fast" languages, such as C, C++, Rust or Zig,
you will need to eventually learn about UB anyway.
Rust handles UB differently from any other language I have seen.
See the end of this page.

Also, UB can be useful:
it lets the optimizer make more assumptions about your code, and hence optimize better.
See [performance docs](perf.md) for details.


## Garbage values

For example, let's look at this program:

```python
import "stdlib/io.jou"

def main() -> int:
    arr = [1, 2, 3]

    sum = 0
    for i = 0; i < 4; i++:
        sum += arr[i]
    printf("%d\n", sum)

    return 0
```

This is supposed to calculate `1 + 2 + 3`, so it should print 6.
On my system it prints **-115019848**.
If I run the program again, it instead prints **1308074024**.
In fact, it seems like I get a different value every time.
The problem is that the loop reads one element beyond the end of the array,
so whatever garbage happens to be in the computer's memory at that location
gets converted to an integer and added to `sum`.


## Randomly working and not working

Here's another common way to get garbage values:

```python
import "stdlib/io.jou"
import "stdlib/str.jou"

def make_string(n: int) -> byte*:
    result: byte[50]
    sprintf(result, "foo%d", n)
    return result

def main() -> int:
    printf("%s\n", make_string(3))
    return 0
```

When I run this repeatedly on my computer, I sometimes get `foo3` and sometimes a blank line:

```
akuli@akuli-desktop:~/jou$ ./jou a.jou

akuli@akuli-desktop:~/jou$ ./jou a.jou

akuli@akuli-desktop:~/jou$ ./jou a.jou

akuli@akuli-desktop:~/jou$ ./jou a.jou
foo3
akuli@akuli-desktop:~/jou$ ./jou a.jou
foo3
akuli@akuli-desktop:~/jou$ ./jou a.jou

akuli@akuli-desktop:~/jou$ ./jou a.jou

```

The `make_string()` function uses `sprintf()` from [stdlib/str.jou](../stdlib/str.jou)
to create a string that looks like `"foo3"`.
It then returns it as a `byte*`.
For convenience, Jou converts `byte[50]` strings to `byte*` strings implicitly
(works with any size of byte array),
so the function actually returns a pointer to the first character of the string.

This program contains UB, because it reads from a pointer into a local variable that no longer exists.
More specifically, it tells `printf()` to read from a local variable inside `make_string()`,
but because the return value of `make_string()` is used as an argument to `printf()`,
the call to `make_string()` is evaluated first.
Once `make_string()` has returned, its local variables no longer exist,
and as you would expect, it is UB to access pointers that point into them.

A simple fix is to return the entire array from `make_string()`, not just the first character.
In other words, we change `-> byte*` to `-> byte[50]`.
This gives us a new compiler error on a different line:

```
compiler error in file "a.jou", line 10: cannot create a pointer into an array that comes from a function call (try storing it to a local variable first)
```

Line 10 is `printf("%s\n", make_string(3))`.
The compiler is trying to convert the array into a pointer here,
because `printf()` wants a pointer.
If we just do like the error message suggests,
we end up storing the array in `main()`, which is great because it no longer vanishes unexpectedly:

```python
import "stdlib/io.jou"
import "stdlib/str.jou"

def make_string(n: int) -> byte[50]:
    result: byte[50]
    sprintf(result, "foo%d", n)
    return result

def main() -> int:
    s = make_string(3)
    printf("%s\n", s)  # Output: foo3
    return 0
```

This code does not contain UB, and works as expected.


## Perfectly working program with UB

Let's modify the example from earlier by making an array of `byte`s instead of `int`s.
Then the program prints 6 every time as expected:

```python
import "stdlib/io.jou"

def main() -> int:
    arr = [1 as byte, 2 as byte, 3 as byte]

    sum = 0
    for i = 0; i < 4; i++:
        sum += arr[i]
    printf("%d\n", sum)  # Output: 6

    return 0
```

This program still contains UB, and it should be fixed.
I make no guarantees of anything working as expected when your program contains UB.
For example, your code might suddenly stop working when you [enable optimizations](perf.md).


## Crashing and valgrind

A different thing happens if we read array elements way beyond the end of the array, rather than just one index beyond.

```python
import "stdlib/io.jou"

def main() -> int:
    arr = [1, 2, 3]

    sum = 0
    for i = 0; i < 10000; i++:
        sum += arr[i]
    printf("%d\n", sum)

    return 0
```

Here's what running this code looks like on my Linux system:

```
akuli@akuli-desktop:~/jou$ ./jou a.jou 
Segmentation fault
```

`Segmentation fault` means that
the program tried to access memory that doesn't belong to it.
Only a small part of the computer's memory belongs to our program,
and when it accesses memory beyond that area, the operating system notices it and kills the program.

The `Segmentation fault` error message doesn't mention the file name and line number (`a.jou` 8) where the crash happened.
It doesn't even mention the function name (`main()`).
If you are on Linux, you can install valgrind (e.g. `sudo apt install valgrind`) and invoke Jou with `--valgrind`.
If you need to debug a crash and you are not on Linux, please create an issue on GitHub.

Running Jou with `--valgrind` looks like this:

```
akuli@akuli-desktop:~/jou$ ./jou a.jou 
==12317== Invalid read of size 4
==12317==    at 0x401180: main (in /home/akuli/jou/jou_compiled/a/a)
==12317==  Address 0x1fff001000 is not stack'd, malloc'd or (recently) free'd
==12317== 
==12317== 
==12317== Process terminating with default action of signal 11 (SIGSEGV)
==12317==  Access not within mapped region at address 0x1FFF001000
==12317==    at 0x401180: main (in /home/akuli/jou/jou_compiled/a/a)
==12317==  If you believe this happened as a result of a stack
==12317==  overflow in your program's main thread (unlikely but
==12317==  possible), you can try to increase the size of the
==12317==  main thread stack using the --main-stacksize= flag.
==12317==  The main thread stack size used in this run was 8388608.
Segmentation fault
```

The relevant parts of the error message are:

```
==12317== Invalid read of size 4
==12317==    at 0x401180: main (in /home/akuli/jou/jou_compiled/a/a)
```

Here `Invalid read` means that we tried to read memory that doesn't belong to the program,
and `size 4` means we tried to read 4 bytes at a time.
Because `int` is 4 bytes, seeing 4 bytes usually means that the code is trying to access an `int` value.

It means that the crash happened in the `main()` function.
To see this better, let's modify the code so that multiple functions are involved in the crash:

```python
def foo() -> int:
    arr = [1, 2, 3]
    sum = 0
    for i = 0; i < 10000; i++:
        sum += arr[i]
    return sum

def bar() -> int:
    return foo()

def main() -> int:
    bar()
    return 0
```

Now I get:

```
==12715== Invalid read of size 4
==12715==    at 0x401180: foo (in /home/akuli/jou/jou_compiled/a/a)
==12715==    by 0x4011A5: bar (in /home/akuli/jou/jou_compiled/a/a)
==12715==    by 0x4011AF: ??? (in /home/akuli/jou/jou_compiled/a/a)
==12715==    by 0x4011B5: main (in /home/akuli/jou/jou_compiled/a/a)
==12715==  Address 0x1fff001000 is not stack'd, malloc'd or (recently) free'd
==12715== 
==12715== 
==12715== Process terminating with default action of signal 11 (SIGSEGV)
==12715==  Access not within mapped region at address 0x1FFF001000
==12715==    at 0x401180: foo (in /home/akuli/jou/jou_compiled/a/a)
==12715==    by 0x4011A5: bar (in /home/akuli/jou/jou_compiled/a/a)
==12715==    by 0x4011AF: ??? (in /home/akuli/jou/jou_compiled/a/a)
==12715==    by 0x4011B5: main (in /home/akuli/jou/jou_compiled/a/a)
==12715==  If you believe this happened as a result of a stack
==12715==  overflow in your program's main thread (unlikely but
==12715==  possible), you can try to increase the size of the
==12715==  main thread stack using the --main-stacksize= flag.
==12715==  The main thread stack size used in this run was 8388608.
Segmentation fault
```

The relevant lines are:

```
==12715==    at 0x401180: foo (in /home/akuli/jou/jou_compiled/a/a)
==12715==    by 0x4011A5: bar (in /home/akuli/jou/jou_compiled/a/a)
==12715==    by 0x4011AF: ??? (in /home/akuli/jou/jou_compiled/a/a)
==12715==    by 0x4011B5: main (in /home/akuli/jou/jou_compiled/a/a)
```

This means that:
- `foo()` crashed
- `bar()` is the function that called `foo()`
- `main()` is the function that called `bar()`

The `???` is something irrelevant that I don't fully understand. It can be ignored.

Unfortunately valgrind doesn't show see the name of the `.jou` file or any line numbers.
This could be fixed in the Jou compiler.
If you run into this and it annoys you, please create an issue on GitHub,
or if someone has already created the issue, add a comment to it.


## Rust's approach to UB

I try to add various warnings to Jou, so that the compiler will let you know if you're about to cause UB.
However, **Jou's compiler warnings will never cover all possible ways to get UB.**
Let me explain why.

Rust is the only language I have seen that checks for all UB when compiling the code.
Practically, this means that:
- you need to convince the Rust compiler that your code does not have UB, and **it is hard**
- the Rust programming language has various complicated things that let programmers communicate UB related things to the compiler (e.g. lifetime annotations)
- sometimes you see `unsafe { ... }`, which basically disables Rust's compile-time checks.

I don't want any of this in Jou.
I want Jou to be a simple, straight-forward and small language, a lot like like C.
Also, making a Rust-like language is much harder,
so if I tried to turn Jou into something similar to Rust, it would never be as good as Rust.
On the other hand, many people get annoyed with various things in C,
so it makes sense to create a new C-like programming language.

That said, I think Rust is a great choice if you need something fast and correct,
and you have a lot of time and patience to learn a new language.
For example, I have written [catris](https://catris.net/) in Rust.

If you want to eventually learn Rust,
I recommend first learning a language that makes you deal with UB, such as C or Jou.
This way you will appreciate how the Rust compiler makes it impossible to cause UB by accident.
Otherwise you will probably end up hating the Rust compiler (and hence the Rust programming language),
because the compiler complains "too much" about your code.
I have seen this happen to several people.
