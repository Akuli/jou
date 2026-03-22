# File system utilities

This file documents `stdlib/fs.jou`.


## Iterating the contents of a directory

TL;DR:

```python
iter = DirIter{dir = "path/to/some/directory"}
while iter.next():
    printf("%s\n", iter.path)  # path/to/some/directory/file.txt
    printf("%s\n", iter.name)  # file.txt

if iter.error_code != 0:
    printf("Error: %s\n", iter.error_message)
```

The `DirIter` class can be used to loop through the files and folders in a directory
(also known as folder).

When creating a `DirIter`, you should set all unused fields to zero
by e.g. using [the `ClassName{}` syntax](classes.md#instantiating-syntax) as shown above.
You can set the following fields:
- `dir: byte*` is a path to the directory being listed. This is the only field that you must set.
- `include_dot_and_dotdot: bool` can be set to `True`
    if you want to get the special `.` and `..` entries when iterating the directory.
    They are skipped by default.

You should call `iter.next()` repeatedly until it returns `False`.
Return value `True` means that a file or subdirectory was found,
and `iter.path` and `iter.name` were updated accordingly.
Return value `False` means that either an error occurred or the end of the directory was reached.
If `.next()` has already returned `False`, calling `.next()` again returns `False` without doing anything.

The memory used for iterating is freed when `.next()` returns `False`.
This means that you don't need any cleanup,
but to avoid leaking memory and the underlying directory handle,
you shouldn't stop calling `.next()` until you get the `False`.
Please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new)
if you want to stop the iterating early.

After calling `.next()`, you can use the following fields:
- `path: byte*` is the path to the file or subdirectory inside the given `dir`.
    It consists of `dir`, a slash if `dir` does not already end with a slash, and a file or subdirectory name.
    The string in `iter.path` is only valid until the following call to `.next()`,
    so if you want to use the string after the following call to `.next()`,
    you need to make a copy of the string.
    This field is `NULL` if `iter.next()` returned `False`.
- `name: byte*` is the file or subdirectory name without the rest of the path.
    Similarly to `iter.path`, this is only valid until the following call to `.next()`
    and you may need to make a copy.
    This field is `NULL` if `iter.next()` returned `False`.
- `error_code: int` is nonzero if `iter.next()` returned `False` due to an error,
    and zero if no error has occurred.
    This is [a Windows API error number](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-) on Windows
    and an [errno value](../stdlib/errno.jou) on other systems.
- `error_message: byte[512]` is a human-readable error message,
    or an empty string if no error has occurred.

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

    if iter.error_code != 0:
        printf("Error: %s\n", iter.error_message)
        return 1

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


## Finding the current executable

Use `find_current_executable()` to get a path to your executable as a string.
The return value should be freed.

For example, if I save the following file to `/home/akuli/thing.jou` and I run it with `jou thing.jou`,
it prints `/home/akuli/jou_compiled/thing/thing`:

```python
import "stdlib/fs.jou"
import "stdlib/io.jou"
import "stdlib/mem.jou"

def main() -> int:
    path = find_current_executable()
    if path != NULL:
        puts(path)
        free(path)
    return 0
```

The `find_current_executable()` function returns `NULL` if any error occurs.
There is currently no cross-platform way to get information about *why* it failed,
and I recommend printing an error message similar to "cannot locate the currently running executable".
This is by design: if this fails on someone's system,
the system is likely in a weird enough state that whoever is running


## Windows support

On Windows, paths containing non-ASCII characters and very long paths may not work properly.
The reason is that `stdlib/fs.jou` uses the ANSI versions of Windows API functions,
such as `FindFirstFileA` and `FindNextFileA`.
Please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new)
if you need to work with arbitrary Windows paths.
A proper fix for this is planned, but not implemented.
