# Design of joulsp

The Jou langserver reuses previously computed things to a reasonable extent.
The design is simple enough that there's no complicated cache invalidation bugs,
but fancy enough that typing a single character into a file doesn't redo everything.

Design goals:
- Simple design: no version numbers, timestamps, or full-blown query engine
- Fast enough on large Jou projects
- Memory usage does not grow significantly if the langserver is used for a long time
- The langserver is easy to work on (for example, types can be compared with `==` operator)
- Cache invalidation hell should be rare when developing the langserver

Overall, I found it surprisingly difficult to
design a simple but reasonably efficient langserver,
but I'm pretty happy with how the design turned out.


## High-level structure

Let's focus on producing diagnostics (that is, mostly errors and warnings).
Here's what needs to happen for each file:

```
                                    ,-- File analyzing ---------------------.
                                    |                                       |
                                    | Evaluating compile-time if statements |
                                    |            Symbol lookups             |
Source code --> Tokens --> AST ---->|          Function signatures          |
             |          |   |       |             Class fields              |
             |          |   |       |      Other files of the project       |
             |          |   |       |                                       |
             |          |   |       '---------------------------------------'
             |          |   |              /|\                  |       |
             |          |   |               |                   |       |
             |          |   |             Symbol          Symbol lookup |
             |          |   |            lookups             results    |
             |          |   |               |                   |       |
           Errors       |   |               |                   V     Errors
             |          |   |       ,-- Function analyzing ---------.   |
             |          |   |       |                               |   |
             |          |   '------>|    Everything that happens    |   |
             |          |           |  inside a function or method  |   |
             |        Errors        |                               |   |
             |          |           '-------------------------------'   |
             |          |                   |                           |
             |          |                 Errors                        |
             |          '---------------.   |   ,-----------------------'
             '----------------------.   |   |   |
                                    |   |   |   |
                                    V   V   V   V
                                Diagnostics for file
```

Here a symbol lookup means finding a function, method, type (class or typedef), global variable,
or other such thing inside a given `JouFile*`.
Each symbol is either public or private.

For now, function analyzing means determining diagnostics
(e.g. undefined variable names, wrong argument types) for the function,
but later it may be extended to more than diagnostics, such as:
- Types of variables and other intermediate values inside the function
    (useful for hovers, or if joulsp is later turned into a Jou compiler)
- Jump-to-definition information for local variables

The langserver never forgets about a file entirely:
if a file has been a part of the project at some point, a `JouFile` remains in memory.
I don't think this will be a problem in practice.
This also means that each `JouFile` doesn't need its own allocator.
Just use `malloc()` for allocations tied to a `JouFile`.


## File Analyzing

Symbol lookups and evaluating compile-time `if` statements
are combined into a single "file analyzing" step,
because they can depend on each other in both directions.
These dependencies can also be cyclic or spread across multiple files.
**This is the only step that deals with multiple files at once.**

```python
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

Analyzing results are stored in symbol tables.
There's one symbol table for each file.
Each symbol table is a hash table with symbol names as keys and analyzing results as values.

File analyzing is done with lazy evaluation.
Here are some examples that hopefully explain how this works:
- If the user's project only uses `printf()`,
    and the user doesn't open `stdlib/io.jou` in the editor to have a look inside,
    we don't analyze every symbol of `stdlib/io.jou` in full detail.
- It's useful to know that `stdlib/str.jou` doesn't define a `printf()`,
    so that when looking up `printf()`, we can quickly put "not found" into the symbol table of `stdlib/str.jou`.
    To help with that, the parser produces a list of possible symbol names
    that could be defined somewhere in the file.
    The parser doesn't know how compile-time `if` statements end up being evaluated,
    so this list may contain symbol names that are not really defined in the file.
- Symbol tables are caches, not sources of truth.
    For example, the symbol table of `stdlib/io.jou` might not contain `puts()` if you never used `puts()`.
    "Not found" is a valid analyzing result that can go in a symbol table,
    and that's how you know for sure that a symbol does not exist.
- Cycles are handled with marking.
    In the above example, we end up trying to get the symbol for `B` or `C` recursively,
    and when we get to the inner call, the symbol has already been marked as being currently analyzed.

For now, all symbol tables are cleared when the AST of any file changes anywhere else than inside a function or method.
To detect this, we simply compare the old and new ASTs without recursing into function bodies.
I think it would be possible to figure out which symbols can remain cached,
but this seems like a rabbit hole, and I don't want to think about it yet.

To avoid getting many errors in code that uses symbols that contain errors,
such as `A`, `B` and `C` in the above example,
there is an `Unknown` type and an `Unknown` value that are emitted for errors.
So if you are working on some part of the langserver, and your code receives an `Unknown` type or value,
you should try to accept it without causing more errors,
because there is already an error somewhere else.


## Function Analyzing

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


## Caching the Diagnostics

The final diagnostics sent to the langserver depend on a lot of different things,
so we just recompute them instead of trying to predict whether they will change.
We keep around a hash of the previous diagnostics,
so that diagnostics are sent to the editor only for files whose diagonstics changed.


## Types

For both convenience and performance, I wanted to compare types with a simple `==` operator.
This means that types should be either pointers or some kind of IDs.
I chose pointers, because that also lets me do things like `puts(some_type.name)` while debugging.

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


## Summary of what gets cached

In other words: where to look if you have one of those nasty cache invalidation bugs.

- File size, mtime (modified time), and current time just before each `JouFile` was opened
- ASTs (one per `JouFile`)
    - Depends only on the file's source code
- Function analyzing results
    - Depends on:
        - the function's AST (128-bit hash with function-relative line numbers)
        - results of symbol lookups (hits and misses)
- Diagnostics for each file
    - Depends on a lot of different things, recomputed every time
    - One 128-bit hash per file
