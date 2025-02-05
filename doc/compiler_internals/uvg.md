# Undefined Value Graphs

UVGs are used to determine which variables may be undefined when the code runs.
This file explains how they work using examples.


## `set` and `use`

Consider the following Jou code:

```python
import "stdlib/io.jou"

def foo(a: int) -> int:         # line 3
    x = a + 6                   # line 4
    y: int                      # line 5
    z = y                       # line 6
    printf("%d %d\n", x, y, z)  # line 7
```

Running `jou --uvg-only file.jou` prints the following UVG:

```
===== UVG for foo =====
block 0 (start):
    [line 3]   set a
    [line 4]   use a
    [line 4]   set x
    [line 6]   use y
    [line 6]   set z
    [line 7]   use x
    [line 7]   use y
    [line 7]   use z
    [line 7]   use return
    Return from function.
```

Let's look at this in more detail:
- Line 3 sets variable `a` because it is the argument of the function.
- Line 4 uses variable `a` to compute `x`. This is fine because the value of `a` has been set.
- Line 5 does not show up at all. Creating a variable is not an instruction in UVG, and all variables exist already when the function begins.
- Line 6 uses `y`, which is undefined. There will be a warning. It also assigns a value to `z`.
- Line 7 uses `y` and `return`. Here `return` is a special variable that represents the return value of the function. It is undefined because the `return` statement is missing.

This causes the compiler to show the following warnings:

```
compiler warning for file "foo.jou", line 6: the value of 'y' is undefined
compiler warning for file "foo.jou", line 7: the value of 'y' is undefined
compiler error in file "foo.jou", line 7: function 'foo' must return a value, because it is defined with '-> int'
```


## The "don't analyze" instruction

Sometimes the code does something that is too complicated for the compiler to analyze.
For example:

```python
import "stdlib/io.jou"

def bar() -> None:                  # line 3
    a, b: int                       # line 4
    scanf("%d\n", &a)               # line 5
    printf("%lld\n", &b as long)    # line 6
```

The UVG for this function is:

```
===== UVG for bar =====
block 0 (start):
    [line 5]   don't analyze a
    [line 6]   don't analyze b
    Return from function.
```

The "don't analyze" UVG instruction means that
the address of the variable has been used in some complicated way.
From that point on, it is not possible to determine whether the variable is defined or undefined.
For example, a function call with `&a` might store `a` to a global variable and set its value later.
No matter what you do after a variable is marked as "don't analyze", the compiler will not complain.

Even a simple variable assignment can introduce the "don't analyze" instruction.
For example, consider the following:

```python
import "stdlib/io.jou"

def baz() -> None:
    x: int
    y = &x              # line 5
    scanf("%d\n", y)    # line 6
```

The UVG is:

```
===== UVG for baz =====
block 0 (start):
    [line 5]    don't analyze x
    [line 5]    set y
    [line 6]    use y
    Return from function.
```

Just setting `y = &x` emits the "don't analyze" instruction for `x`.
This way, when we see `scanf("%d\n", y)`,
we don't need to somehow know which variables might end up in the variable `y`.
That might be doable, but for now, it seems unnecessarily complicated.


## Variable Statuses and Branching

To implement the warnings in
[tests/should_succeed/undefined_variable.jou](../../tests/should_succeed/undefined_variable.jou),
the Jou compiler keeps track of the possible statuses of the variables in UVG.
The **status** of a variable is conceptually a subset of the two-element set `{defined, undefined}`.
In reality, the "don't analyze" instruction makes this slightly more complicated (but not much).

Examples:
- A variable with status `{undefined}` is surely undefined. For example, `x` just after `x: int`.
- A variable with status `{defined, undefined}` may be defined or undefined.
    For example, a variable inside a loop before a value is assigned to it is like this:
    a previous iteration may leave the variable in a `defined` state.
- A variable whose status is the empty set has not been analyzed yet.

In UVG, branching and loops are handled just like in LLVM.
UVG instructions are placed into **blocks**.
The end of the block is a **terminator**, which defines what happens at the end of the block.
Most importantly, it contains information about which blocks we may jump into.
For example, an `if`/`else` statement may jump into the `if` or the `else`.

The statuses are figured out block by block.
The compiler internally stores the status of each variable at the end of each block in UVG.
They are first set to empty sets and then filled in block by block,
revisiting blocks multiple as needed to handle loops.

Pseudo code:

```python
statuses_at_end = {b: {v: set() for v in variables} for b in blocks}

def analyze_block(b, warn=False):
    statuses = {}
    for v in variables:
        statuses[v] = set()
        for sourceblock in blocks_that_jump_to_b:
            statuses[v] |= statuses_at_end[sourceblock][v]
        if b == the_start_block:
            statuses[v].add("undefined")

    for ins in b.instructions:
        (update statuses based on ins)
        if warn and (ins uses an undefined value):
            show a warning

    if statuses_at_end[b] != statuses:
        statuses_at_end[b] = statuses
        for destblock in blocks_that_b_jumps_to:
            queue.add(destblock)

queue = {the_start_block}
while queue:
    analyze_block(queue.pop(), warn=False)
for b in all_blocks:
    analyze_block(b, warn=True)
```

Initially, the possible statuses of values come from other blocks that jump into the block being analyzed.
Also, all values may be undefined in the beginning of the start block.

When we have computed the statuses of variables at the end of a block,
and they differ from the previous analysis,
we take the blocks that used the outdated statuses and queue them for another round of analyzing.
The algorithm will terminate eventually as long as a cyclic change of statuses
(e.g. `foo -> bar -> baz -> foo`) is not possible.

Once all that is done, we know which statuses are possible at the start and end of each block,
Basically, control flow has been taken care of.
This means that it's time to show warnings.

This could be done in a way that loops over the instructions fewer times, but that is unnecessary,
because this code is not the performance bottleneck of the compiler.
