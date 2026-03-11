# File system utilities

This file documents `stdlib/fs.jou`.

In this documentation (and in general), "directory" and "folder" mean the same thing.


## Iterating contents of a directory

The `DirIter` class can be used to loop through the files and folders in a directory
(also known as folder).
Here's how it's used:

```python
iter = DirIter{dir = "path/to/some/directory"}
while iter.next():
    do_something(iter.path)  # joined with slash: path/to/some/directory/file.txt
    do_something(iter.name)  # just the name: file.txt
```

As you can see, `.next()` returns a `bool`.
Return value `True` means that a file or subdirectory was found,
and `iter.path` and `iter.name` were updated accordingly.

The same memory is reused between calls to `.next()`,
so if you want to use the string in `iter.path` or `iter.name`
after the following call to `.next()`, you need to make a copy of it.

The memory used for iterating is freed when `.next()` returns `False`.
This means that you don't need any cleanup,
but to avoid leaking memory, you shouldn't stop calling `.next()` until you get the `False`.
Please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new)
if you want to stop the iterating early.

The iteration order is whatever the operating system and file system happen to produce,
and you shouldn't rely on it.
For example, you can [sort the strings](sorting.md#sorting-strings):

```python
import "stdlib/fs.jou"
import "stdlib/io.jou"
import "stdlib/list.jou"
import "stdlib/mem.jou"
import "stdlib/sort.jou"
import "stdlib/str.jou"

def main() -> int:
    results = List[byte*]{}

    iter = DirIter{dir = "doc/images"}
    while iter.next():
        results.append(strdup(iter.name))

    sort_strings(results.ptr, results.len)

    # Output: 64bit-meme-small.jpg
    # Output: 64bit-meme.jpg
    # Output: sources.txt
    for i = 0; i < results.len; i++:
        puts(results.ptr[i])
        free(results.ptr[i])  # Free the copy created with strdup()

    free(results.ptr)
    return 0
```

When you create a `DirIter`, you can set the following fields of `DirIter`:
- `dir: byte*` is the path to the folder to be listed.
    This is the only field that must be set.
- `include_dot_and_dotdot: bool` can be set to `True`
    if you want to get the special `.` and `..` entries when iterating the directory.
    They are skipped by default.

If an errors occurs while reading the directory, it causes `.next()` to return `False`.
For example, an empty directory and a non-existent directory behave the same way:
`.next()` returns `False` immediately.
If you need to know why `.next()` returned `False` on operating systems other than Windows,
you can use [stdlib/errno.jou](../stdlib/errno.jou).
If you need to do that in a cross-platform way,
please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new).


## Windows support

On Windows, paths containing non-ASCII characters and very long paths may not work properly.
Please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new)
if you need to work with arbitrary Windows paths.
