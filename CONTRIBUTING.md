This file explains how to develop the Jou compiler.


## Setup

If you have any trouble with this, please create an issue!

<details> <summary>Linux</summary>

Following the [instructions in the README](README.md#setup) is enough.

To edit the C code, you can use any editor that uses `clangd`.
The `make` command creates a file `compile_flags.txt`
to help `clangd` find the LLVM header files.

</details>

<details> <summary>64-bit Windows</summary>

1. Download and install Git from [Git's website](https://git-scm.com/download/win) if you don't have it already.
2. Open Git Bash from the start menu.
    **You must use Git Bash** for running bash scripts such as `windows_setup.sh` and `runtests.sh`.
3. Clone the project with the command prompt:
    ```
    cd Desktop
    git clone https://github.com/Akuli/jou
    ```
    You can put the project anywhere. The above command places it on the desktop.
4. Run a script that does the rest of the setup for you:
    ```
    cd jou
    ./windows_setup.sh
    ```
    If you have a slow internet connection
    and it takes a long time for `windows_setup.sh` to download mingw64,
    you can instead run `./windows_setup.sh --small`.
    This way it uses `mingw64-small.zip`,
    which is just like the usual mingw64, but with many large files deleted to make it smaller.
    I created it locally on my computer.
    If you don't want to trust it, you can run `windows_setup.sh` without `--small`
    or look at how `.github/workflows/windows.yml` compares `mingw64-small.zip` to the original `mingw64.zip`.
5. Compile Jou:
    ```
    source activate
    mingw32-make
    ```
    The `source activate` command adds `C:\Users\YourName\Desktop\jou\mingw64\bin` to your PATH,
    where `C:\Users\YourName\Desktop` is the folder where you cloned Jou.
    If you don't want to run it every time you open a Git Bash window to work on Jou,
    you can instead add it to your PATH permanently with Control Panel.
6. Compile and run hello world:
    ```
    ./jou.exe examples/hello.jou
    ```
    You should see `Hello World` printed.
    If you instead get errors about missing DLL files, run `source activate` first.
    The Jou compiler depends on DLLs in `mingw64\bin`,
    so `mingw64\bin` must be in PATH when running it.
7. Run tests:
    ```
    ./runtests.sh
    ```

</details>


## How does the compiler work?

There are two compilers. See [the README](README.md#compilers) for an explanation.
The compilers work very similarly even though they are written in different languages.

At a high level, the compilation steps are:
- **Tokenize:** split the source code into tokens
- **Parse:** build an abstract syntax tree (AST) from the tokens
- **Typecheck:** errors for wrong number or type of function arguments etc, figure out the type of each expression in the AST
- **Build CFG:** build Control Flow Graphs for each function from the AST
- **Simplify CFG:** simplify and analyze the control flow graphs in various ways, generate warnings and errors based on them
- **Codegen:** convert the CFGs into LLVM IR
- **Emit objects:** create `.o` files from the LLVM IR
- **Link:** run a linker that combines the `.o` files into an executable
- **Run:** run the executable

To get a good idea of how these steps work,
you can run the compiler in verbose mode:

```
$ ./jou -v examples/hello.jou   # High-level overview
$ ./jou -vv examples/hello.jou  # Show all details
```

With `-vv` (or `--verbose --verbose`), the compiler shows
the tokens, AST, CFGs and LLVM IR generated.
The control flow graphs are shown twice, before and after simplifying them.
Similarly, LLVM IR is shown before and after optimizing.

After making changes to the compiler, run `make` to recompile it.
To make recompiling faster, only the stage 3 compiler (`./jou` or `jou.exe`)
will be recompiled.
All stages of bootstrapping are recompiled if any file in `bootstrap_compiler` is modified (or `touch`ed).


## Tests

GitHub Actions runs all tests when you make a pull request,
so you don't need to run tests locally if you only intend to fix a couple small things.
That said, test-driven development works very well for developing compilers.
There should be a test (or a TODO comment about adding a test)
for every feature and for every compiler error/warning.

Running tests (if on Windows, use Git Bash):

```
$ ./runtests.sh
```

The `runtests.sh` script does a few things:
- It compiles the Jou compiler if you have changed the compiler since the last time it was compiled.
- It runs all Jou files in `examples/` and `tests/`. To speed things up, it runs two files in parallel.
- It ensures that the Jou files output what is expected.

The expected output is auto-generated from comments in the Jou files:

- A comment like `# Output: foo` appends a line `foo` to the expected output.
- A comment like `# Error: foo` on line 123 of file `tests/bar/baz.jou` appends a line
    `compiler error in file "tests/bar/baz.jou", line 123: foo`.
- A comment like `# Warning: foo` on line 123 of file `tests/bar/baz.jou` appends a line
    `compiler warning for file "tests/bar/baz.jou", line 123: foo`.
- Files in `examples/` and `tests/should_succeed/` should run successfully (exit code 0).
    All other files should cause a compiler error (exit code 1).

If the actual output doesn't match the expected output, you will see diffs where
green (+) is the program's output and red (-) is what was expected.
The command that was ran (e.g. `./jou examples/hello.jou`) is shown just above the diff,
and you can run it again manually to debug a test failure.
You can also put e.g. `valgrind` or `gdb --args` in front of the command.

Because running tests is slow, you often want to run only one test, or only a few tests.
For example, maybe you want to run all Advent of Code solutions.
To do things like this, the test script takes a substring of the file path as an argument,
and runs only tests whose path contains that substring.
For example, `./runtests.sh aoc` finds files like `examples/aoc2023/day03/part2.jou`.

```
$ ./runtests.sh aoc         # run Advent of Code solutions
$ ./runtests.sh class       # run tests related to defining classes
$ ./runtests.sh ascii_test  # run tests for the "stdlib/ascii.jou" module
```

You can use `--verbose` to see what test files get selected:

```
$ ./runtests.sh ascii_test --verbose
```

To find missing `free()`s and various other memory bugs,
you can also run the tests under valgrind
(but this doesn't work on Windows, because valgrind doesn't support Windows):

```
$ sudo apt install valgrind
$ ./runtests.sh --valgrind
```

This doesn't do anything with tests that are supposed to fail with an error, for a few reasons:
- The compiler does not free memory allocations when it exits with an error.
    This is fine because the operating system will free the memory anyway,
    but `valgrind` would see it as many memory leaks.
- Valgrinding is slow. Most tests are about compiler errors,
    and valgrinding would take several minutes if they weren't skipped.
- Most problems in error message code are spotted by non-valgrinded tests.

There are also a few other ways to run the tests.
You can look at `.github/workflows/` to see how the CI runs tests.

To ensure that documentation stays up to date,
it is also possible to run code examples in the documentation as tests:

```
$ ./doctest.sh
```

The `doctest.sh` script finds code examples from markdown files in `doc/`.
It only looks at code examples that contain `# Output:`, `# Warning:` or `# Error:` comments.
It then attempts to run each example and compares the output similarly to `runtests.sh`.


## Windows Release Builds

Using Jou on Linux is easy,
because you can install Jou's dependencies with `apt` (or whatever package manager you have).
To make using Jou on Windows equally easy, or at least as painless as possible,
users can download a zip file that contains everything needed to run Jou.
See the README for instructions to install Jou from a zip file.

A zip file is built and released in GitHub Actions every morning at 4AM UTC,
or when triggered manually from GitHub's UI
(e.g. to release an important bugfix as quickly as possible).
No new release is made if there are no new commits.

Some parts of the build are done in `.github/workflows/windows.yml`,
and the rest is in `.github/workflows/release.yml`.
The difference is that `windows.yml` runs on every pull request and on every push to `main`,
but `release.yml` runs daily and when triggered manually.
This means that:
- `windows.yml` should do most of the build.
    It should also run tests on the build results to make sure that everything works.
    If something in this file stops working, it will be noticed very quickly when someone makes a pull request.
- `release.yml` should be simple and small.
    If `release.yml` breaks, it might take a while for someone to notice it.
    Ideally it would only download the build results of `windows.yml` and create a release.

There is also `jou --update`.
On Linux it simply runs `git pull` and recompiles the Jou compiler.
On Windows it runs a PowerShell script that downloads and installs the latest release from GitHub Actions.
