# Analyzer Design

The analyzer is the part of the language server that finds errors in your code.
In the future, the same analyzer will be used in the Jou compiler to reduce code duplication.

The main challenge with designing the analyzer is making it respond fast enough
to small source code edits (e.g. user presses a key).
Specifically, the design goals are:
- Fast enough on large Jou projects
- Simple design: no version numbers, timestamps, or full-blown query engine
- Memory usage does not grow significantly if the langserver is used for a long time
- The langserver is easy to work on (for example, types can be compared with `==` operator)
- Cache invalidation hell should be rare when developing the langserver

I found it surprisingly difficult to design something that satisfies these goals.
Basically, it seems like I need to choose one of following downsides:
- Recompute everything every time the user types one character
- Build a full-blown query system with support for dependencies and versioning
- Cache invalidation hell

I first tried making a query engine, but it became very complicated very quickly,
so I chose cache invalidation hell.
Surprisingly, I'm quite happy with the resulting design.
After several iterations of designing, the cache invalidation part is quite simple and predictable.


## Conventions

In Jou, a method `do_thing()` in `class Foo` is basically a function named `Foo.do_thing`.
When this document discusses "functions", it also includes methods.

We assume that 128-bit hashes never have hash collisions.
Currently the hash function is [MurmurHash3_x64_128](https://stackoverflow.com/questions/11899616/murmurhash-what-is-it).


## High-Level Structure

Let's consider producing diagnostics (that is, mostly errors and warnings).
Once those work, it should be easy enough to add other langserver features.
Here are all the things that need to happen:

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
             |          |           | inside a function (or method) |   |
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

Here a symbol lookup means finding a function (or method), class, `typedef`, global variable,
`const` constant, or other such thing inside a given `JouFile*`.
Each symbol is either public or private.

For now, function analyzing means determining diagnostics
(e.g. undefined variable names, wrong argument types) for the function,
but later it may be extended to more than diagnostics, such as:
- Types of variables and other intermediate values inside the function
    (useful for hovers, or if joulsp is later turned into a Jou compiler)
- Jump-to-definition information for local variables

The langserver never forgets about a file entirely:
if a file has been a part of the project at some point, a `JouFile` remains in memory.
I don't think this will be a problem, and if it is, this can be cleaned up later.
This also means that each `JouFile` doesn't need its own allocator.
Just use `malloc()` for allocations tied to a `JouFile`.


## The `analyze()` function

The interface of the analyzer is super simple: **just one function** called `analyze()`.

In the langserver, `analyze()` is called after updating VFS with one or more input messages from the editor.
For example, if the user is typing something into a file named `foo.jou` and the langserver is somewhat laggy,
many small "one character was added to `foo.jou`" messages are processed before calling `analyze()`,
and the `analyze()` call sees the resulting state of the VFS.
For more details on this, see [io-and-replay.md](io-and-replay.md).

Inputs of `analyze()`:
- Analyzer state
    - Includes mode, either **langserver mode** or **compiler mode**, depending on how the analyzer is used.
- VFS (Virtual File System, a fancy name for file contents stored in memory)
- Array of `VfsFile*`
    - In langserver mode, this is all the files that are open in the editor.
    - In compiler mode, just the one file passed on command line.
    - Below, "a file that should be fully analyzed" means any file of this array in langserver mode, and any `JouFile*` whatsoever in compiler mode.

First, for each file in the array given to `analyze()`, a corresponding `JouFile*` is created if it doesn't exist yet.
A `JouFile*` means a file in the analyzer state.

Next `analyze()` parses all files whose content has changed since last time
(VFS provides a content hash for quickly checking this) and all newly added files.

Now `analyze()` can do all the analyzing except looking inside function bodies.
This is skipped if during all the parsing we did so far, old and new AST of all `JouFile*`s
were the same everywhere except inside function bodies.
If there were any changes outside function bodies (or any new files):
- Clear the [symbol cache](#symbol-cache) and [type cache](#type-cache) **for all files**.
    - This is a simple way to handle cross-file dependencies.
    - These caches are stored in the analyzer state.
    - After this, we can do symbol lookups lazily again.
    - [Function analyzing cache](#function-analyzing-cache) is **not** cleared.
    - This seems slow, but [I think it will be fine](#why-clearing-symbol-cache-and-type-cache-should-be-fast-enough).
- Evaluate all compile-time `if` statements. For each imported file that doesn't yet have a corresponding `JouFile*`, create it and parse the file.
    - These two cannot be separated into two different steps,
        because imports can be inside compile-time `if` statements.
- Analyze everything except function bodies in all files that should be fully analyzed (this concept was defined above).
- Delete outdated entries from [function analyzing cache](#function-analyzing-cache),
    including functions that no longer exist.

At this point no new files will be parsed during the `analyze()` call.
So, if we are in langserver mode, let's clean up files that are no longer needed using simple mark-and-sweep garbage collection:
- Mark every `JouFile*` corresponding to a `VfsFile*` in the array given to `analyze()`.
- Recursively mark every `JouFile*` imported by a marked `JouFile*`
- Sweep unmarked `JouFile*`s

Lastly, `analyze()` analyzes function bodies in all files that should be fully analyzed (this concept was defined above).
[Function analyzing cache](#function-analyzing-cache) is used to skip the analyzing
when an up-to-date result already exists.


### Why clearing symbol cache and type cache should be fast enough

- Most of the work is function analyzing, not symbol lookup.
- In Jou, it is rare to re-export symbols imported from another file.
    Also, a user can't have very many files open in the editor,
    so the total number of files that the langserver knows about is somewhat small.
- Unused symbols are ignored due to laziness in symbol lookup.
    For example, if you `import stdlib/io.jou"` and you only use `printf()`,
    symbols like `class FILE` and `puts()` won't be analyzed at all.

Ultimately, I'll find out when the langserver is usable enough for large Jou projects.


## Symbol Cache

Conceptually, the symbol cache is a mapping whose keys are `(JouFile*, symbol name)` pairs, and each value is a `NULL` (not found) or a `Symbol*`.
For example: value corresponding to key `(JouFile* of stdlib/io.jou, "printf")` might be a function symbol.

The same `Symbol*` can appear in multiple files.
For example, if `foo.jou` imports `stdlib/io.jou` and uses `printf`,
the keys `(JouFile* of foo.jou, "printf")` and `(JouFile* of stdlib/io.jou, "printf")` will point to the same `Symbol*`.

The symbol cache is populated lazily to support cyclic dependencies.
This means that if the symbol table doesn't contain a symbol,
it means that we haven't tried to look it up yet and it might still exist.
Here are some examples of dependencies between symbols:

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

Detecting cycles is easy to do by making the symbols available early,
but marking them as being currently analyzed.

To avoid getting many errors in code that uses symbols that contain errors,
such as `A`, `B` and `C` in the above example,
there is an `Unknown` type and an `Unknown` value that are emitted for errors.
So if you are working on some part of the langserver, and your code receives an `Unknown` type or value,
you should try to accept it without causing more errors,
because there is already an error somewhere else.


## Function Analyzing Cache

To reanalyze functions only when needed, we cache function analyzing results as well as their dependencies.

Conceptually, the function analyzing cache is a mapping:
- Keys: `(JouFile*, function name)`
    - `JouFile*` is just the pointer needed to identify the file, not its contents.
        This is similar to using the file path, but faster and less fragile (consider e.g. case-insensitive file systems).
    - For methods, the "function name" looks like `SomeClass.some_method`.
- Values: `(128-bit hash of function's AST, list of symbol lookups, analyzing results)`
    - Hash of AST includes the signature (function name, AST for arguments, AST for return type).
    - Symbol lookups list includes the lookups needed for signature and body.
        The result of each lookups is also included, including "not found" results.
    - Line numbers are relative to start of function, so that
        adding an empty line to start of file doesn't cause every function to be reanalyzed.

To clarify how symbol lookups are hashed, suppose that the following code is in `foo.jou`:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("hello\n")
    printf("hi\n")
    return 0
```

The hash of `main()` function analyzing results consists of:
- `JouFile*` pointer that represents `foo.jou`
- hash of the `main()` function's AST
- symbol lookup: `int` in file `foo.jou` → built-in type `int`
- symbol lookup: `printf` in file `foo.jou` → function symbol, file `stdlib/io.jou`, signature `(byte*, ...) -> int`

Each symbol lookup is stored only once, because often the same symbols are used several times.


## Hashing Types

The [function analyzing cache](#function-analyzing-cache) needs to hash symbols and types.
It can't hash `Symbol*` or `Type*` pointers directly,
because they are destroyed and recreated every time any AST changes outside a function body.
They are hashed by content instead.

A recursive function takes care of hashing types.
To handle cyclic structures, recursion depth is used when the same type is encountered for a second time.
For example, consider the following:

```python
class Foo:
    x: int
    bar: Bar
    y: int

class Bar:
    foo_ptr: Foo*
```

To compute `hash(Foo)` (pseudo-code), roughly the following happens:

- `hash(Foo)` begins. `Foo` is marked as being currently hashed at depth 1.
    - `hash("x")`
    - `hash(int)`
    - `hash("bar")`
    - `hash(Bar)` begins. `Bar` is marked as being currently hashed at depth 2.
        - `hash("foo_ptr")`
        - `hash(Foo*)` begins
            - `hash(Foo)` returns `1`, not an actual hash
        - `hash(Foo*)` is done
    - `hash(Bar)` is done. `Bar` is marked as not being currently hashed.
    - `hash("y")`
    - `hash(int)`
- `hash(Foo)` is done. `Foo` is marked as not being currently hashed.

Note that if a type refers to itself recursively, the hash cannot be cached like you might expect:
the result of `hash(Bar)` would be different if it was hashed by itself rather than as a member of `Foo`.
It is easy to detect this as part of the hashing process:
a type points to something cyclic if the chain of recursive calls returned the depth at some point.

In reality, instead of making many small hashes like `hash("bar")`,
we update the same hasher object.
It is important to place boundaries properly, so that `ping: uint64` hashes differently than `pingu: int64`.


## Type Cache

Jou has several types that can point at other types:
- User-defined classes
- `T*` (pointer)
- `T[N]` (array)
- `List[T]` (generic class, `List` is just an example)
- `funcptr(T1, T2, T3) -> T4`

Comparing these with `==` means that these need to be stored somewhere.
For example, if type `int*` already exists,
we can't allocate a new `int*` when the user does `&some_int`.
A field on the `Type` representing `int` would work for pointers and arrays,
but not `funcptr`'s or generics with 2 or more parameters.

To make these lookups efficient, these `Type*` pointers are stored in the type cache.
Conceptually, the type cache is a mapping whose
keys are enough information to find a type (e.g. "array of 10 ints") and each value is a `Type*`.

A `Type*` is **never modified** after it goes into the type cache.
For example, to add a new field to an existing class,
do not pull its `Type*` out of the type cache and modify it.
Instead, just create a new `Type*` at the right time when the type cache has just been cleared.


## `Unknown`

To somewhat avoid a single syntax error causing many errors in other parts of a project,
there is an `Unknown` type and an `Unknown` value that are emitted for errors.
So if you are working on some part of the langserver, and your code receives an `Unknown` type or value,
you should try to accept it without causing more errors,
because there is already an error somewhere else.


## Future Ideas

- It's useful to know that `stdlib/str.jou` doesn't define a `printf()`,
    so that when looking up `printf()`, we can quickly put "not found" into the symbol table of `stdlib/str.jou`.
    To help with that, maybe the parser should produces a list (or bloom filter?) of possible symbol names
    that could possibly be defined somewhere in the file?


## TODO

- implement everything
