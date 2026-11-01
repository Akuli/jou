# Design of joulsp

The Jou langserver reuses previously computed things to a reasonable extent.
The design is simple enough that there's no complicated cache invalidation bugs,
but fancy enough that typing a single character into a file doesn't redo everything.

Design goals:
- Simple design: no version numbers, timestamps, or full-blown query engine
- Fast enough on large Jou projects
- Memory usage does not grow surprisingly if the langserver is used for a long time
- The langserver is easy to work on (for example, types can be compared with `==` operator)
- Cache invalidation hell should be rare when developing the langserver

Overall, I found it surprisingly difficult to
design a simple but reasonably efficient langserver,
but I'm pretty happy with how the design turned out.


## High-level pipeline

Fundamentally, here's what needs to happen for each file:

```
Source code --> Tokens --> AST -->  Symbol Table
                            |           |
                            |           V
                            |`--->  Function analyzing results
                            |           |
                            |           V
                            `---->  Diagnostics for file
```

The symbol table contains the names and types of
functions, methods, types (classes and typedefs), global variables,
and other such things defined in the same file.
Each symbol is either public or private,
and the public symbols of other files are looked up on-demand, not copied here.
The symbol table and everything before it is rebuilt from scratch
every time the file content changes.

For now, function analyzing means determining diagnostics
(e.g. undefined variable names, wrong argument types) for the function,
but later it may be extended with more than diagnostics, such as:
- Types of variables and other intermediate values inside the function
    (useful if joulsp is later turned into a Jou compiler)
- Jump-to-definition information for local variables


## Caching for function analyzing

The whole function is always analyzed even if only a small part of it changes,
and the results of function analyzing are cached.
The idea is that analyzing one function is fast enough,
but analyzing every function in a large file could be too slow to be done on every keystroke.

Function analyzing depends only on:
- AST of the function
- Global symbol lookups and their results (each lookup first searches from the current file, then imported files)

Because this is **only 2 things**, it's easy enough to check
whether a function needs to be reanalyzed.

Caveats with the AST:
- Adding a newline to the beginning of the file shouldn't cause
    every function in the file to be reanalyzed
    only because line numbers in AST have changed.
    To fix this, line numbers inside each function are relative to the start of the function.
    The `def` keyword is always on line 1.
- Keeping the function's AST around for comparing would require
    allocating it with a separate allocator in the parser, copying it, or
    keeping around old parsing results.
    Instead, we compute a 128-bit FNV-1a hash of the function's AST.

Caveats with symbol lookups:
- It's possible to place imports under `if somewhat_complicated_condition:`,
    so it could take some time to eliminate the top-level `if` statements from AST.
    This is done every time the file content changes.
    I don't think this will be a problem in practice.
- During analyzing, every symbol lookup done is stored in a list,
    and when checking whether the function needs to be reanalyzed,
    we re-run the same lookups.
    This is more complicated than checking other checks mentioned here,
    but still easy enough.
- The list of symbol lookups is deduplicated to avoid checking the same things several times.
- The list of symbol lookups needs to also contain failed symbol lookups:
    if `printf` didn't exist before but the user has now added `import "stdlib/io.jou"`,
    the "not found" diagnostic must go away.
- It is important to also look up the argument types.
    For example, suppose that a function uses `argument.some_field`, and the user deletes `some_field` from the class definition.
    The function will get reanalyzed,
    because looking up the argument type will now produce a different `Type*`.

All this also applies to methods. A method is basically a function with a fancy name.


## Types

For both convenience and performance, I wanted to compare types with a simple `==` operator.
This means that types should be either pointers or some kind of IDs.
I chose pointers, because that also lets me do things like `puts(some_type.name)`.

For classes, each `Type*` represents **one version** of the class and its fields
(not methods, those are treated as functions).
If a member is added or removed, you need a new `Type*`,
and every function that used the old `Type*` will be reanalyzed.

Jou also has several types that point at other types (also known as structural types):
- `T*` (pointer)
- `T[N]` (array)
- `List[T]` (generic class, `List` is just an example)
- `funcptr(T1, T2, T3) -> T4`

Comparing these with `==` means that these need to be stored somewhere.
For example, if type `int*` already exists,
we can't allocate a new `int*` when the user does `&some_int`.
A field on the `Type*` representing `int` would work for pointers and arrays,
but not `funcptr`'s or generics with 2 or more parameters.
To make these lookups efficient, all `Type*` pointers are stored in a hash table.

The next problem is that the big hash table of types wants to grow forever and consume a large amount of memory.
To fix this, we use simple mark-and-sweep garbage collection:

1. Mark all the types that are still being used by some cached results.
    Also mark built-in types like `int` so that they won't be swept away.
2. For each marked type, find out which other types it depends on:
    `int*[2]` depends on `int*`, which depends on `int`.
    Mark those too. Recurse.
3. Sweep unmarked types.
4. Clear the marks of remaining types.


## Input and Output

TODO: design the details, how to buffer the input etc


## Summary of what gets cached

In other words: where to look if you have one of those nasty cache invalidation bugs.

TODO
