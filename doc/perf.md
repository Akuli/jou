# Performance and optimizations

Because Jou uses [LLVM](https://llvm.org/),
it is about as fast as languages like C, C++ or Rust,
and much faster than many interpreted languages (e.g. Python).
You can also enable optimizations to make your Jou code run faster.

To see this, let's write the same sample program in Python, C and Jou:

```python
# fib40.py (Python)
def fib(n):
    if n <= 1:
        return n
    return fib(n-1) + fib(n-2)

print("fib(40) =", fib(40))
```

```c
// fib40.c
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
# fib40.jou
declare printf(format: byte*, ...) -> int

def fib(n: int) -> int:
    if n <= 1:
        return n
    return fib(n-1) + fib(n-2)

def main() -> int:
    printf("fib(40) = %d\n", fib(40))
    return 0
```

Each of these programs computes the 40th
[Fibonacci number](https://en.wikipedia.org/wiki/Fibonacci_number)
using a very inefficient algorithm.
This is dumb if you actually want to compute Fibonacci numbers,
but it is a simple example of code that takes a while to run.
For example, the Python program runs in **39.5 seconds**:

```
$ time python3 fib40.py
fib(40) = 102334155

real    0m39,552s
user    0m39,401s
sys     0m0,024s
```

In bash, you can see how a command runs by putting `time` in front of it.
You can ignore the `user` and `sys` lines
and focus only on the line starting with `real`.

We can similarly measure how long the C and Jou programs run.

```
$ clang fib40.c && time ./a.out
fib(40) = 102334155

real	0m1,056s
user	0m1,036s
sys	0m0,000s

$ time ./jou fib40.jou
fib(40) = 102334155

real	0m2,715s
user	0m2,711s
sys	0m0,004s
```

Here are the results:

| Python        | C (with the clang compiler)   | Jou           |
|---------------|-------------------------------|---------------|
| 39.5 seconds  | 1.05 seconds                  | 2.71 seconds  |

Note that an interpreted language like Python is quite slow in comparison.
In an interpreted language, you typically avoid doing anything that requires good performance,
and you instead do those things using a different language.
For example, in Python it is common to use `numpy`, a library written in C.

Another interesting thing is that Jou appears to be about 2-3 times slower than C.
However, the Jou program runs in only **0.47 seconds** if we enable optimizations:

```
$ time ./jou -O3 fib40.jou
fib(40) = 102334155

real	0m0,473s
user	0m0,469s
sys	0m0,004s
```

Here the `-O3` flag tells Jou to optimize the code as much as possible.
The number after `-O` must be between 0 and 3, and it tells how much to optimize.
The default is `-O0`, which means that Jou doesn't optimize the code at all.

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

Here are the results, after running each command a few times and averaging the results:

| Optimization flag | C (with the clang compiler)   | Jou           |
|-------------------|-------------------------------|---------------|
| `-O0` (default)   | 1.05 seconds                  | 2.71 seconds  |
| `-O1`             | 0.67 seconds                  | 0.67 seconds  |
| `-O2`             | 0.48 seconds                  | 0.48 seconds  |
| `-O3`             | 0.48 seconds                  | 0.48 seconds  |

Here's what we can learn from these experiments:
- Enabling optimizations can make your code run faster.
- Interpreted languages are slow.
    In this case, Python was about 15 times slower than unoptimized Jou
    and about 80 times slower than Jou with `-O2` or `-O3`.
- Unoptimized Jou is slower than unoptimized C.
    However, as soon as you enable the optimizations, even if it's just `-O1`,
    the differences go away and Jou is just as fast as C.
- Performance is usually a bit random: even though we measured 0.473 secnds above,
    it is closer to 0.48 than 0.47 on average.
    (As a side note, some people say that it is better to use
    the *minimum* of the measured times than their average.
    I personally don't have an opinion on this.)
- Enabling more optimizations doesn't necessarily make your code run faster.
    For example, we got 0.48 seconds with both `-O2` and `-O3`.

Also, note that I used the `clang` C compiler,
because it uses LLVM and Jou also uses LLVM.
The results would not be so consistent with Jou
if I used a different C compiler, such as `gcc`.


## Why is `-O3` not the default?

Given that optimizations make the code run faster,
you are probably wondering why they aren't enabled by default in Jou.
It is easier to work with optimizations disabled (or with only `-O1`),
for two reasons:
1. Optimizing a large program is slow, even though the optimized program will run faster.
    A lot of time spent waiting for the optimizer to run makes for an annoying workflow.
2. The optimizer assumes that your code doesn't do some dumb things.
    If your code does these things, the results can be surprising

Let's explore these things with more examples.


### Time spent optimizing

TODO


### What the optimizer assumes

Let's write a program that crashes if the user wants it to crash.

```
declare printf(msg: byte*, ...) -> int
declare getchar() -> int

def main() -> int:
    printf("Crash this program? (y/n) ")
    if getchar() == 'y':
        foo: int* = NULL
        x = *foo
    return 0
```

Here `foo: int* = NULL` creates a NULL pointer (TODO: document pointers).
Then `x = *foo` attempts to read a value of the NULL pointer,
which will fail on any modern operating system.

Let's run the program. If I type a letter other than `y`, such as `n`,
the program exits without crashing.
If I type `y`, the program crashes with a segmentation fault.

```
$ ./jou asd.jou
Crash this program? (y/n) n
$ ./jou asd.jou
Crash this program? (y/n) y
Segmentation fault
$
```

But with optimizations enabled, the program does not crash even if I type `y`:

```
$ ./jou -O3 asd.jou
Crash this program? (y/n) y
$
```

The optimizer assumes that you don't attempt to access the value of a `NULL` pointer.
In other words, it thinks that the `x = *foo` code will never run,
and it can therefore be ignored.

Correctly written Jou code will not break when optimizations are enabled.
For example, accessing a `NULL` pointer simply isn't a good way to exit a program.
If you want something similar to a crash, you can use the `abort()` function:

```
declare printf(msg: byte*, ...) -> int
declare getchar() -> int
declare abort() -> void

def main() -> int:
    printf("Crash this program? (y/n) ")
    if getchar() == 'y':
        abort()
    return 0
```

Now the program crashes when I type `y`, even if optimizations are enabled:

```
$ ./jou -O3 asd.jou
Crash this program? (y/n) y
Aborted
```
