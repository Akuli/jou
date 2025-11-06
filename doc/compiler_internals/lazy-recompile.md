# Lazy Recompiling

Compiling a large Jou project (such as the Jou compiler) is slow.
By far the slowest part is compiling code to `.o` files (object files) with LLVM.
To avoid invoking LLVM when possible,
the compiler reuses previously compiled `.o` files if they exist and they are up to date.

For example, at the time of writing this, it takes 8 seconds to compile the Jou compiler for the first time,
but if I compile it again, it compiles in less than one second.
This is because the second compilation reuses `.o` files created when compiling for the first time.


## Why not File Modified Time (mtime)

This is the most traditional way to decide when to recompile a file.
Most build tools (such as `make` and `ninja`) use this approach.

The idea is to look at the "modified time" of the files, also known as **mtime**.
When a file is modified, the file system remembers *when* it was modified,
and the file's mtime is set to the current time.
For example, if `foo.jou` is newer than `foo.o`,
it means we cannot reuse the previously compiled `foo.o`.
If `foo.jou` used to contain `printf("Hello Wrodl")`
and it was changed to `printf("Hello World\n")`,
the `foo.o` file still contains the wrong print in a compiled form.

While this approach works, it has several downsides:
- An unimportant change to a file changes the mtime and will cause it to be recompiled.
    For example, the file is rebuilt if you fix a typo in a comment.
- Multiple source files (in our case, Jou files) may affect the compilation of a `.o` file.
    In other words, each `.o` file can depend on multiple different Jou files.
    Determining the dependencies is difficult in Jou.
    Simply depending on all `import`ed files is not enough,
    because it is possible to call methods defined in files that are not `import`ed.
    If such a method changes a parameter from `int` to `int64`, it must be passed differently,
    so all calls to the method must be recompiled as well.
- Timestamps work differently depending on both the OS and the underlying filesystem.
    For example, mtime may always appear as zero for some reason.
- Weird things will happen if mtime is in the future.
    This may happen if the user notices that the computer's clock is showing the wrong time,
    moves it back, and then continues programming.
    This can also happen if you extract a `.zip` file or similar
    created on another computer whose clock is set to a different time.


## Hashes in File Names

Jou does not use file modified timestamps for the above reasons.
Instead, we do something much simpler.
We include hashes in the file name,
and we simply check if a file with the correct name exists already.

When you run `jou examples/hello.jou`, you end up with files that look something like this:

```
examples/
└── jou_compiled/
    └── hello/
        ├── _assert_fail_69fbb4a31067cf2b_c836422383205631.o
        ├── hello
        ├── hello_b79384c3bc4f7e71_206571b75665c619.o
        └── io_73a880ec4a91db7d_9bd7fe21c89ad529.o
```

Here's how the files are named:

    hello_b79384c3bc4f7e71_206571b75665c619.o
    |___| |______________| |______________|
      |          |                |
      |          |                |
    Simple    Content        Identifier
     name      hash             hash

The file name starts with **a simple name**
whose purpose is to make the file name more human-readable.
The compiler does not rely on it in any way.
Note that the simple name may contain underscores.

The **content hash** depends on all data that is given to LLVM when compiling.
It changes when LLVM would produce a different result.
For example, it will stay the same if you fix a typo in a comment,
but it will change if you modify `examples/hello.jou` to print something else than "Hello World".
This is implemented by feeding the AST to
[a builder](../../compiler/builders/hash_builder.jou) that is just like
[the LLVM builder](../../compiler/builders/llvm_builder.jou)
except that it builds a hash instead of LLVM IR.

The **identifier hash** is a hash of the file path `"examples/hello.jou"` and a few other things.
It is used to delete old object files.
Before the compiler creates `foo_1111222233334444_5555666677778888.o`,
it deletes existing files whose name ends with `_5555666677778888.o`.
This means that if you edit and recompile a file 1000 times,
you don't end up with 1000 object files in the `jou_compiled` folder,
because the identifier hash is the same every time.

Here are some more examples of technical details
to clarify how the two hashes are supposed to work.

- The content hash depends on the Jou compiler binary,
    but for most changes to the compiler, identifier hashes do not change.
    This means that if you update the Jou compiler,
    the new compiler will likely delete all `.o` files that the old compiler compiled.
- The identifier hash depends on the optimization level,
    so if you compile with `-O1` and with `-O2`,
    you will end up with two `.o` files for each Jou file.
