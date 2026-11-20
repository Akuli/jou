# Design of joulsp

The Jou langserver reuses previously computed things to a reasonable extent.
The design is simple enough that there's not too many cache invalidation bugs,
but fancy enough that typing a single character into a file doesn't redo absolutely everything.

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


## Cache Invalidation

It seems to me that when designing a langserver,
I need to choose one of two following two things,
and alleviate the pain to the extent that I can:
- Building a full-blown query system with supports for dependencies and versioning
- Cache invalidation hell

I first tried to design a query system, but as expected, it became very complicated very quickly,
so I chose to deal with cache invalidation instead.

Here are all the things that we cache for file analyzing and function analyzing.
In other words, look at these if you get a nasty cache invalidation bug.
- `JouFile` for every file we have ever looked at
    - Hash of file contents ([VFS](#virtual-file-system) takes care of reading files reasonably)
    - AST
    - 128-bit hash of diagnostics sent to the langserver (diagnostics depend on basically everything, so we just recompute them every time and compare with the hash)
- Symbol cache
    - Conceptually, a mapping whose keys are `(JouFile*, symbol name)` pairs and each value is a `NULL` (not found) or a `Symbol*`
    - Example: value corresponding to key `(JouFile of stdlib/io.jou, "printf")` is a function symbol
    - Populated lazily to support cyclic dependencies
    - If the symbol table doesn't contain a symbol, it means we didn't try to look it up yet
    - Same `Symbol*` can appear in multiple files
    - Example: `("my_file.jou", "printf")` exists if we have analyzed code that contains `printf("hello\n")`, even if user forgot to import `stdlib/io.jou`
- Type cache
    - Conceptually, a mapping whose keys are enough information to find a type (e.g. "array of 10 ints") and each value is a `Type*`
    - A `Type*` is **never modified** after it goes into the type cache.
        For example, to add a new field to an existing class,
        do not pull its `Type*` out of the type cache and modify it.
        Create a new `Type*` instead.
- Function analyzing results
    - Conceptually, a mapping whose keys include all the things listed below, and values are results of function analyzing
    - Keyed by the following things baked into a single 128-bit hash:
        - `JouFile*`
        - Function's AST
            - Includes the signature: function name, arguments, return type
            - Line numbers are relative to start of function, so that adding an empty line to start of file doesn't cause every function to be reanalyzed
        - [Symbol hashes](#symbol-hashes) of all the symbol lookups performed while parameter types and return type (if any)
- Diagnostics for each file
    - Depends on a lot of different things, recomputed every time
    - One 128-bit hash per file

The 128-bit hashes are assumed to never collide.


Many edits of a file (e.g. adding a comment) do not affect the AST at all,
and those are easy enough to detect by just comparing the old and new AST.
But what exactly should we do when the AST changes?


## Symbol Hashes




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


## AI draft

TODO: rewrite this into actual documentation, it is a much simpler design than the rest of what's here now

----




Here is a clean draft of your language server caching design doc, capturing your depth-based cycle handling, function-level memoization, and the pragmatic trade-offs you chose.

---

# Design Doc: Function-Level Caching for the Jou Language Server

**Status:** Draft

**Target Language:** Jou (Akuli)

---

## 1. Problem & Goals

A full query-and-versioning engine (such as Salsa) adds significant complexity for a relatively small language like Jou. At the same time, naive full-project reanalysis on every edit is wasteful—especially when a user edits a rarely used type in a widely imported module (like `utils.jou`).

### Key Observations About Jou & IDE Usage

* **Bounded open files:** A user typically only has a small number of files open in the editor at once.
* **Selective symbol usage:** Imported files do not need full deep analysis upfront; only the symbols actually referenced need to be resolved/inspected.
* **Shallow re-export chains:** Re-exporting symbols from other files is rare in Jou, keeping cross-file lookup chains short, though cyclic type/symbol references are common.
* **Fast top-level/symbol collection:** Re-running non-function-body analysis when needed is fast enough that caching *function body analysis* provides the main speedup.

### Goals

1. Cache function analysis results (including methods, treated uniformly as functions with qualified/special names) using a **128-bit cache key** with negligible collision probability.
2. Reanalyze only functions whose inputs/dependencies have actually changed.
3. Keep the invalidation model simple, predictable, and easy to maintain.

---

## 2. Caching Strategy

### What Is Cached

* **Cached:** Analysis results for individual functions/methods (types, diagnostics, resolved references, etc., as produced by function-body analysis).
* **Not cached (recomputed as needed):** Top-level/global symbol discovery and resolution leading into function analysis.

### Cache Invalidation Rules

A cached analysis entry for a function is considered obsolete if:

1. There is a **newer** analysis entry/version for the same function name, **or**
2. The current AST **no longer contains** a function with that name.

*(Note: Because the cache key itself encodes the inputs observed during function analysis, an unchanged function with unchanged inputs naturally hits the cache via its 128-bit key.)*

---

## 3. Designing the 128-Bit Function Cache Key

A function's cache key is a **128-bit hash** built from:

1. The function's own AST/definition (relevant to its body/signature).
2. All global symbol lookups performed during function analysis, **including both hits and misses**—for example:
* Lookup miss: "`printf` was not found in `stdlib/str.jou`"
* Lookup hit: "`printf` was found in `stdlib/io.jou` with signature `<...>`"


3. Definitions of classes/types referenced during analysis (including fields, types, declaration order, and conditional compilation outcomes).

### Hashing Practices

* **Domain tagging:** Prefix every hashed item (lookup hit, lookup miss, field, type, back-reference) with a distinct tag and length/boundary markers so distinct inputs cannot serialize to the same byte stream.
* **128-bit digest:** Use a strong 128-bit hash algorithm (e.g., xxHash3-128) so accidental collisions are practically nonexistent.

---

## 4. Handling Cyclic Classes with Recursion Depth

Classes and types in Jou can reference themselves directly or mutually (e.g., `Foo` has a `Bar*` field and `Bar` has a `Foo` field). A naive recursive hash would loop infinitely.

### Depth-Based Cycle Markers

During a single hash traversal:

* Track the **current recursion depth** (e.g., `1` at the start of hashing a class, incrementing as recursion descends into nested types, and resetting to `0` when unwinding).
* If hashing a class encounters a class that is **already active on the current call stack**, emit a cycle/back-reference marker containing that **active recursion depth** instead of recursing further.
* Once a class finishes hashing, reset its active depth so it is no longer treated as an active ancestor.

### Trade-offs & Implications

* **Works for self-cycles and mutual cycles:** Because the depth refers to a frame on the active call stack, cycles terminate cleanly and bake the cycle structure into the hash.
* **Not memoizable per-class across differing call depths:** Because the same class can be reached at different depths depending on the traversal path or entry point, per-class hash results aren't simply cached standalone by class alone—instead, the depth-aware traversal runs as part of computing the function's 128-bit key.
* **Future optimization path:** If profiling later shows class/type hashing becoming a bottleneck in large graphs, this area is isolated and can be refined without changing the overall function-cache architecture.

---

## 5. Open Questions / Future Tweaks to Watch

* **Cache eviction:** Decide on a retention cap (such as a max cache size or max entries per function name) so long editing sessions don't accumulate unbounded stale entries.
* **Diagnostics & source locations:** Verify whether any diagnostics stored in a cached function result depend on line/column numbers of external declarations that might shift during edits outside the function.
* **Conditional compilation (`if SOME_CONSTANT:`):** Ensure constant conditions affecting visible fields/symbols are reflected consistently in lookups and hashed type shapes.

---

Would you like to expand any section of this draft—such as concrete pseudocode for the cache key builder or a section on error/diagnostic handling?
