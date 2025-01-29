# Undefined Value Graphs

UVGs are used to determine which values may be undefined when the code runs.

For example, consider the following Jou code:

```python
import "stdlib/io.jou"

def foo(a: int) -> int:         # line 3
    x = a + 6                   # line 4
    y: int                      # line 5
    z = y                       # line 6
    printf("%d %d\n", x, y, z)  # line 7
```

Running `jou --uvg-only file.jou` prints the following UVG representation of the code:

```
===== UVG for foo =====
block 0 (start):
    [line 3]   set $1 to &a
    [line 3]   set *$1
    [line 3]   set $3 to &x
    [line 3]   set $5 to &y
    [line 3]   set $7 to &z
    [line 4]   use *$1
    [line 4]   set *$3
    [line 6]   use *$5
    [line 6]   set *$7
    [line 7]   use *$3
    [line 7]   use *$5
    [line 7]   use *$7
    [line 7]   use return
    Return from function.
```

Here `$1`, `$3`, `$5` and `$7` are temporary values within the UVG.
They are emitted because the compiler thinks of all variable access as happening through pointers.
Because `$1` is always `&a`, `$3` is always `&x` and so on,
the compiler will eliminate these before it analyzes the UVG further.
Here is the resulting, simpler UVG:

```
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
- Line 5 does not show up at all. Creating a variable is not an operation in UVG, and all variables exist (with undefined values) at start.
- Line 6 uses `y`, which is undefined. There will be a warning.
- Line 7 uses `y` and `return`. Here `return` is a special variable that represents the return value of the function. It is undefined because the `return` statement is missing.

This causes the compiler to show the following warnings:

```
compiler warning for file "foo.jou", line 6: the value of 'y' is undefined
compiler warning for file "foo.jou", line 7: the value of 'y' is undefined
compiler error in file "foo.jou", line 7: function 'foo' must return a value, because it is defined with '-> int'
```

In UVG, branching and loops are handled just like in LLVM.
UVG instructions are placed into **blocks**.
The end of the block is a **terminator**, which can be:
- jump to another block
- branch: jump to one of two blocks depending on some value
- return from function
- unreachable: the end of the block will never run for some reason


## Value Statuses

To implement the warnings in
[tests/should_succeed/undefined_variable.jou](tests/should_succeed/undefined_variable.jou),
the Jou compiler keeps track of the possible statuses of the variables in UVG.
The **status** of a variable is a subset of the following:
- `undefined`
- `defined` (some value has been assigned to the value, but we don't know what value)
- `points to foo`, where `foo` is another variable in the UVG

For example, a variable with status `{undefined, points to foo}`
is either undefined or `&foo` depending on some `if` statement or loop or other control flow thing.

The statuses are figured out block by block.
The compiler internally stores the status of each variable at the end of each block in UVG.
They are first set to empty sets and then filled in block by block,
revisiting blocks multiple as needed to handle loops.

Pseudo code:

```python
statuses_at_end = {b: {v: set() for v in values} for b in blocks}

def analyze_block(b, warn=False):
    statuses = {}
    for v in values:
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

At this point, we know which statuses are possible, and control flow has basically been taken care of.
Because we only stored the statuses at the end of each block,
we must loop through the instructions in each block again to show warnings.

This could be done in a way that loops over the instructions fewer times, but that is unnecessary,
because this code is not the performance bottleneck of the compiler.
