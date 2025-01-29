Undefined Value Graphs are used to determine which values may be undefined when the code runs.

For example, consider the following Jou code (5 lines):

```python
def foo(a: int) -> int:
    x = a + 6
    y: int
    z = y
    printf("%d %d\n", x, y, z)
```

Here's how the compiler builds a UVG for this code.
Because of the first line `def foo(a: int) -> int:`, we set the value of variable `a`.
In UVG's, it's not relevant what the value is.
We only care about whether the value is undefined.

```
set a
```

For the second line `x = a + 6`, we first take the address of `x`
and store it to an anonymous value `$1`.

```
set $1 to address of x
```

We then evaluate the right side `a + 6`.
We don't need to do anything to evaluate `a`.
To evaluate `6`, we create another another anonymous value `$2`.
To evaluate `a + 6`, we mark `a` and `6` as used, and we place the result to yet another anonymous value `$3`.

```
set $2
use a
use $2
set $3
```

Now we have `a + 6` and we store it to the variable `x` through the pointer `$1`:

```
use $3
set value of pointer $1
```

Later the UVG analyzer will notice that `$1` is always going to be the address of `x`,
and replace `set value of pointer $1` with `set x`.
That's how it knows that `x` cannot be undefined when it's printed.

Here's the UVG for the remaining code. Note that no UVG is needed for `y: int`.

```
set $4 to address of z
use y
set value of pointer $4
set $5      # $5 = the string "%d %d\n"
use $5
use x
use y
use z
set $6      # $6 = return value of printf()
use return
Return from function.
```

Here `return` represents the return value of the function.
We `use` it but never `set` it, so the compiler shows a warning
about the missing `return` statement in the Jou code.
We also used `y` a couple times without setting it, so we get the following warnings:

```
compiler warning for file "foo.jou", line 4: the value of 'y' is undefined
compiler warning for file "foo.jou", line 5: the value of 'y' is undefined
compiler error in file "foo.jou", line 5: function 'foo' must return a value, because it is defined with '-> int'
```

In UVG, branching and loops are handled just like in LLVM.
UVG instructions placed into **blocks**.
The end of the block is a **terminator**, which can be:
- jump to another block
- branch: jump to one of two blocks depending on some value
- return from function
- exit the whole program


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
