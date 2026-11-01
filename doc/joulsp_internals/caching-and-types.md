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


## High-level structure

Let's focus on producing diagnostics (that is, mostly errors and warnings).
Fundamentally, here's what needs to happen for each file:

```
             ,-----------------------------------------------------> Errors --------.
             |          ,------------------------------------------> Errors --------+
             |          |                                   ,------> Errors --------+
             |          |                                   |                       |
             |          |           ,-- File analyzing -------------------------.   |
             |          |           |                                           |   |
             |          |           |   Evaluating compile-time if statements   |   |
             |          |           |                                           |   |
Source code --> Tokens --> AST ---->|              Symbol lookups               |   |
                            |       |                                           |   |
                            |       |        Other files of the project         |   |
                            |       |                                           |   |
                            |       '-------------------------------------------'   |
                            |               |  |                                    |
                            |               |  `--> Symbol lookup results   ,-------'
                            |               |                               |
                            |               |                               |
                            |               |                               |
                            |               V                               V
                            '--> Function analyzing results  -->  Diagnostics for file
```

Here a symbol lookup means finding a function, method, type (class or typedef), global variable,
or other such thing.
Each symbol is either public or private.

For now, function analyzing means determining diagnostics
(e.g. undefined variable names, wrong argument types) for the function,
but later it may be extended to be more than diagnostics, such as:
- Types of variables and other intermediate values inside the function
    (useful if joulsp is later turned into a Jou compiler)
- Jump-to-definition information for local variables


## File analyzing

Symbol lookups and evaluating compile-time `if` statements
are combined into a single "file analyzing" step,
because they can depend on each other in both directions.
These dependencies can also be cyclic or spread across multiple files.
**This is the only step that deals with multiple files at once.**

```
# Symbol lookup depends on evaluating a compile-time if statement
if True:
    def this_is_a_symbol() -> None:
        pass

# Evaluating a compile-time if statement depends on a symbol lookup
const MY_CONSTANT: bool = True  # MY_CONSTANT is a symbol
if MY_CONSTANT:
    ...

# Example of a cyclic dependency with types (allowed because a pointer is used)
class Foo:
    bar: Bar
class Bar:
    foo_ptr: Foo*

# Example of a cyclic dependency with const (error)
const A: int = B
const B: int = C  # Error: constant C depends on itself
const C: int = B  # Error: constant B depends on itself
```

Cycles are handled with marking.
For example, if we are trying to get the symbol for constant `C`,
but `C` has already been marked as being currently analyzed,
then the result of analyzing `C` is a cyclic dependency error.
In the above example, the result of analyzing `A` is a constant with unknown value.

File analyzing is done with lazy evaluation.
This helps with handling cycles, and should improve performance a lot.
For example, if the user's project only uses `printf()`,
there's no need to analyze every symbol of `stdlib/io.jou` in full detail.

It also helps if we can quickly confirm that `stdlib/str.jou` does not define a `printf()`,
so the parser produces a list of symbol names that could possibly be defined somewhere in the file.
The parser doesn't know how compile-time `if` statements end up being evaluated,
so this list may contain names that are not actually defined anywhere in the file.

For now, there is no cache for file analyzing.
In other words, all file analyzing results are thrown away and recomputed
every time the AST of any file changes.
I think it would be possible to add caching,
but it seems like a rabbit hole, and I don't want to do it unless it's really needed.


## Function analyzing

The whole function is always analyzed when anything inside it changes,
and the results of function analyzing are cached.
The idea is that analyzing one function is fast enough,
but analyzing every function in a large file could be too slow to be done on every keystroke.

Function analyzing depends only on:
- AST of the function
- Results of symbol lookups (each lookup searches the current file and public symbols of imported files)

Because there is **only 2 things**, it's easy enough to check
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


## Caching the diagnostics




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

The next problem is that the big hash table of types
wants to grow forever and consume a large amount of memory.
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


## What gets cached

In other words: where to look if you have one of those nasty cache invalidation bugs.

- ASTs (one per file)
    - Depends only on the file's source code
- Function analyzing results
    - Depends on:
        - the function's AST (128-bit hash with function-relative line numbers)
        - results of symbol lookups (hits and misses)
- Diagnostics for each file
    - Depends on 
