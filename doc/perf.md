# Performance and Optimizations

Because Jou uses [LLVM](https://llvm.org/),
it is faster than interpreted languages like Python,
and you can enable optimizations to make your Jou code run faster.
Jou is about as fast as C, C++ or Rust.

To see this, let's write a simple but slow program in Python, C and Jou.

```python
# fib40.py (Python program)
def fib(n):
    if n <= 1:
        return n
    return fib(n-1) + fib(n-2)

print("fib(40) =", fib(40))
```

```c
// fib40.c (C program)
#include <stdio.h>

int fib(int n) {
    if (n <= 1)
        return n;
    return fib(n-1) + fib(n-2);
}

int main()
{
    printf("fib(40) = %d\n", fib(40));
    return 0;
}
```

```python
# fib40.jou (Jou program)
import "stdlib/io.jou"

def fib(n: int) -> int:
    if n <= 1:
        return n
    return fib(n-1) + fib(n-2)

def main() -> int:
    printf("fib(40) = %d\n", fib(40))
    return 0
```

Each program computes the 40th Fibonacci number.
The first two Fibonacci numbers are 0 and 1.
After that, you always get the next one by adding the previous two:
the third Fibonacci number is 0+1 = 1, the fourth is 1+1 = 2,
the fifth is 1+2 = 3 and so on:

```
0 1 1 2 3 5 8 13 21 34 ...

0+1=1
  1+1=2
    1+2=3
      2+3=5
         ...
```

Here's how the `fib()` function in each program works:
- If `n` is zero or one, it is returned unchanged,
    so the first two Fibonacci numbers are `fib(0) == 0` and `fib(1) == 1`.
- To compute any other Fibonacci number, the `fib()` function
    calls itself to calculate the previous two Fibonacci numbers
    and adds them.
    For example, `fib(2)` calculates `fib(0) + fib(1)` and returns 1 (`0 + 1 = 1`),
    and `fib(3)` computes `fib(1) + fib(2)` and returns 2 (`1 + 1 = 2`).

This is a very slow way to calculate Fibonacci numbers, because passing
a large number to the `fib()` function makes it call itself many times.
On my computer, it takes **39.5 seconds** for the Python program to calculate `fib(40)`:

```
$ time python3 fib40.py
fib(40) = 102334155

real    0m39,552s
user    0m39,401s
sys     0m0,024s
```

In bash, you can see how long a command runs by writing `time` in front of it.
You can ignore the `user` and `sys` lines
and focus only on the line starting with `real`.

We can similarly measure how long the C and Jou programs run.

```
$ clang fib40.c && time ./a.out
fib(40) = 102334155

real    0m1,056s
user    0m1,036s
sys     0m0,000s

$ time ./jou -O0 fib40.jou
fib(40) = 102334155

real    0m2,715s
user    0m2,711s
sys     0m0,004s
```

All three programs computed the same number, but in different amounts of time:

| Python        | C (with the clang compiler)   | Jou           |
|---------------|-------------------------------|---------------|
| 39.5 seconds  | 1.05 seconds                  | 2.71 seconds  |

Note that an interpreted language like Python is quite slow in comparison.
In an interpreted language,
you typically use a library written in a different language to work around this.
For example, in Python it is common to use `numpy` to perform calculations,
and because `numpy` is written in C, it is much faster than pure Python code.

In our test Jou appears to be about 2-3 times slower than C.
However, the Jou program runs in only **0.47 seconds** if we enable optimizations:

```
$ time ./jou -O3 fib40.jou
fib(40) = 102334155

real    0m0,473s
user    0m0,469s
sys     0m0,004s
```

Here the `-O3` flag tells Jou to optimize the code as much as possible.
The number after `-O` must be between 0 and 3, and it tells how much to optimize.

**Note:** We previously passed the `-O0` flag to Jou,
which meant that it didn't optimize the code at all.
In a future version of Jou, `-O0` will probably become the default.

To be fair, C compilers also accept
`-O0`, `-O1`, `-O2` and `-O3` options that work in the same way.
Let's compare Jou and C with each optimization flag:

```
$ time ./jou -O0 fib40.jou
$ time ./jou -O1 fib40.jou
$ time ./jou -O2 fib40.jou
$ time ./jou -O3 fib40.jou
$ clang fib40.c -O0 && time ./a.out
$ clang fib40.c -O1 && time ./a.out
$ clang fib40.c -O2 && time ./a.out
$ clang fib40.c -O3 && time ./a.out
```

After running each command a few times the result averaged the following:

| Optimization flag     | C (with the clang compiler)   | Jou           |
|-----------------------|-------------------------------|---------------|
| `-O0`                 | 1.05 seconds                  | 2.71 seconds  |
| `-O1` (Jou default)   | 0.67 seconds                  | 0.67 seconds  |
| `-O2`                 | 0.48 seconds                  | 0.48 seconds  |
| `-O3`                 | 0.48 seconds                  | 0.48 seconds  |

These experiments have shown that:
- While enabling optimizations can make your code run a lot faster,
 enabling more of them might not necessarily speed it up even more. For example, we got 0.48 seconds with both `-O2` and `-O3`.
- Unoptimized Jou is slower than unoptimized C,
    but with optimizations enabled, Jou is just as fast as C.
- Interpreted languages are slow.
    In this case, Python was about 15 times slower than unoptimized Jou
    and about 80 times slower than Jou with `-O2` or `-O3`.

Also, note that I used the `clang` C compiler,
because it uses LLVM and Jou also uses LLVM.
The results would not be so consistent with Jou
if I used a different C compiler, such as `gcc`.


## Why is `-O3` not the default?

Given that optimizations make the code run faster,
you are probably wondering why not all optimizations are enabled by default in Jou,
and why making `-O0` the default is planned.
It is often easier to work with optimizations disabled (or with `-O1`),
for two reasons:
1. Optimizing a large program is slow. The optimized program will run faster,
    but it takes a while for LLVM to figure out how to make the code faster.
    Waiting for the optimizer to do its thing is annoying. See Example #1
2. The optimizer assumes that your code doesn't contain dumb things, or specifically, things that cause **undefined behavior** (see below). If it does, it may behave unpredictably (crashing, not crashing as expected, some parts of the code not running, etc.). See Example #2 below.

Let's explore these with more examples.


### Example #1: Optimizing a large program is slow

TODO: write this section once a large Jou program exists and name it Example #1


### Example #2: Optimizer's assumptions and undefined behavior

Let's write a program that crashes if the user selects yes.

```python
import "stdlib/io.jou"

def main() -> int:
    printf("Crash this program? (y/n) ")
    if getchar() == 'y':
        foo: int* = NULL
        x = *foo
    return 0
```

The `getchar()` function waits for the user to type a character and press Enter.
If the user types `y`, the program creates a NULL pointer (TODO: document pointers),
and then attempts to read the value from the NULL pointer into a variable `x`.
Reading from a NULL pointer will crash the program on any modern operating system.

Let's run the program. Typing a letter other than `y`, such as `n`,
makes the program exit without crashing.
Typing `y` makes it crash with a segmentation fault.

```
$ ./jou -O0 asd.jou
Crash this program? (y/n) n
$ ./jou -O0 asd.jou
Crash this program? (y/n) y
Segmentation fault
$
```

But with optimizations enabled, the program does not crash even when typing `y`:

```
$ ./jou -O3 asd.jou
Crash this program? (y/n) y
$
```

The optimizations make the program ignore the code to access the value of a `NULL` pointer.
Essentially it thinks that the `x = *foo` code will never run,
because you aren't supposed to access the value of a NULL pointer.
This code will thus get ignored.

Accessing a `NULL` pointer is an example of **undefined behavior**, or **UB** for short.
Undefined behavior is generally a Bad Thing: if your code has UB, you should fix it.
For example, a much better way to crash the program would be using `abort()` function:

```python
import "stdlib/io.jou"
import "stdlib/process.jou"

def main() -> int:
    printf("Crash this program? (y/n) ")
    if getchar() == 'y':
        abort()
    return 0
```

Now the program contains no UB. It crashes when `y` is typed, even if optimizations are enabled:

```
$ ./jou -O3 asd.jou
Crash this program? (y/n) y
Aborted
```

UB is easiest to find and understand when optimizations are turned off.
For example, the optimizer might realize that a large part of the code cannot possibly run without invoking UB,
and hence just delete it, like it deleted our crashing code in the above example.
This would be much more confusing to debug than a crash.

For more about UB, see [the UB docs](ub.md).
