# Imports

Jou's import system is super simple.
There are two kinds of imports:
- **Imports starting with a dot:** e.g. `import "./foo.jou"`, `import "../foo.jou"` or `import "./foo/bar.jou"`
- **Imports starting with `stdlib`:** e.g. `import "stdlib/foo.jou"` or `import "stdlib/foo/bar.jou"`

Any other kind of import is a compiler error.

Imports starting with a dot are resolved relative to where the Jou file is.
For example, if file `/home/akuli/folder/foo.jou` contains `import "./bar/baz.jou"`,
that means `/home/akuli/folder/bar/baz.jou`.
As usual, `..` goes up to the parent directory,
so `import "../test.jou"` would be `/home/akuli/test.jou`.

Imports starting with `stdlib` are resolved relative to where the Jou compiler is.
You usually don't need to care about how exactly the compiler finds the `stdlib` folder.
Look at [compiler/paths.jou](../compiler/paths.jou)
or create an issue on GitHub if you want to know more about this.

Jou's `import` does slightly more than copy/pasting file contents (like C's `#include`),
but not very much more.
Practically, here's what you need to know:
- The same file can be imported multiple times by different files,
    and it will be compiled just once.
    For example, if `bar.jou` imports `foo.jou`, and `baz.jou` imports `foo.jou`,
    the compiler will compile `foo.jou` only once.
- Importing does not affect other files, only the file that contains the import.
    This means that if `utils.jou` defines `a_really_useful_function()`,
    then you need to `import "./utils.jou"` in all files that use `a_really_useful_function()`.
- The order of `import` statements in a file doesn't matter.
    Feel free to arrange them into whichever order you prefer.
    There is no such thing as "the code doesn't compile if I swap these imports"
    (or if there is, please [create an issue](https://github.com/Akuli/jou/issues/new)).


## Public

If you want a function, [class](classes.md), [enum](enums.md) or global variable to be importable,
make it public with the `@public` decorator.
Anything not decorated with `@public` can only be used in the same file.

For example:

```python
def only_for_this_file() -> None:
    ...

@public
def visible_in_files_that_import_this_file() -> None:
    ...

class OnlyForThisFile:
    field: int
    def method(self) -> None:
        ...

@public
class VisibleInFilesThatImportThisFile:
    field: int
    def method(self) -> None:
        ...

global kinda_ok_variable: int

@public
global many_people_consider_this_bad: int
```

Class fields and methods are always public.
This means that if you have an instance of a class,
you can simply call any method defined in the class.
If you don't like this, please create an issue on GitHub to discuss it.

Many people dislike global variables, especially when they are accessed in multiple files.
That said, Jou lets you decorate a global variable as `@public` and import it into another file,
because sometimes that is the best solution.


## Conflicting Names

**You cannot have multiple public things with the same name, even if they are in different Jou files.**
This means that if `bar.jou` defines a function `foo()` with `@public`,
and `baz.jou` also defines a function `foo()` with `@public`,
then you cannot use `bar.jou` and `baz.jou` in the same project.
To work around this, rename the functions or don't use `@public`.

The same applies to anything decorated with `@public`.
However, it's fine to have multiple methods with the same name in different public classes.
For example, [the `ast.jou` file in the Jou compiler](../compiler/ast.jou)
has many classes whose name starts with `Ast` to prevent conflicts with other files,
and most of them have methods named `print()` and `free()`.

Let me explain why Jou has this limitation.
When the Jou compiler has compiled all Jou files individually,
it runs the **linker**, which is a program that combines them into an executable file
(e.g. `jou_compiled/my_program/my_program.exe`).
The linker complains if multiple functions have the same name,
because it doesn't know which function to use.

You might be thinking that other low-level languages allow having multiple functions with the same name.
In reality, they just rename the functions automatically.
For example, consider the following C++ code.
It has two functions named `foo`,
but the code compiles because they are in different C++ namespaces.

```c++
#include <iostream>

namespace bar {
    void foo() {
        std::cout << "foo from bar namespace" << std::endl;
    }
}

namespace baz {
    void foo() {
        std::cout << "foo from baz namespace" << std::endl;
    }
}
```

I compiled this on Linux with a C++ compiler, and the compiler created the following functions:
- `_ZN3bar3fooEv`
- `_ZN3baz3fooEv`

Jou is a simple language that avoids surprising and "magical" things.
If you want two differently named functions in Jou, then **you** need to name them differently:
a Jou function named `foo()` is actually named `foo()`.

Methods are entirely a Jou concept, and they become plain old functions when the Jou code is compiled.
If you have a Jou method `bark()` in Jou class `Dog`,
the compiler generates a function named `Dog.bark`.
This means that debugging tools (e.g. [valgrind](ub.md#crashing-and-valgrind))
show `Dog.bark` as the function name when they tell you something about the Jou method.
