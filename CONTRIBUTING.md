This file explains how to develop the Jou compiler.


## Setup

If you have any trouble with this, please create an issue!

<details> <summary>Linux</summary>

Following the [instructions in the README](README.md#setup) is enough.

</details>

<details> <summary>64-bit Windows</summary>

1. Download and install Git from [Git's website](https://git-scm.com/download/win) if you don't have it already.
2. Download and install Python (unless you are going to use `./windows_setup.sh --small`, see below).
    Any reasonably new version of Python will work.
3. Open Git Bash from the start menu.
    **You must use Git Bash** for running bash scripts such as `windows_setup.sh` and `runtests.sh`.
4. Clone the project with Git Bash (or command prompt):
    ```
    cd Desktop
    git clone https://github.com/Akuli/jou
    ```
    You can put the project anywhere. The above command places it on the desktop.
5. Run a script that does the rest of the setup for you:
    ```
    cd jou
    ./windows_setup.sh
    ```
    If you have a slow internet connection
    and it takes a long time for `windows_setup.sh` to download mingw64,
    you can instead run `./windows_setup.sh --small`.
    Instead of downloading the full mingw64 (about 360MB download),
    this will get a minimal version of MinGW from a [release](#releases) of Jou (about 50MB download).
6. Compile Jou:
    ```
    source activate
    mingw32-make
    ```
    The `source activate` command adds `C:\Users\YourName\Desktop\jou\mingw64\bin` to your PATH,
    where `C:\Users\YourName\Desktop` is the folder where you cloned Jou.
    If you don't want to run it every time you open a Git Bash window to work on Jou,
    you can instead add it to your PATH permanently with Control Panel.

    When you run `mingw32-make` for the first time, it
    [bootstraps Jou from Git history](README.md#bootstrapping).
    If you used the `--small` option of `windows_setup.sh`,
    the bootstrapping process will begin at the downloaded Jou release
    instead of the last version that came with a compiler written in C.
7. Compile and run hello world:
    ```
    ./jou.exe examples/hello.jou
    ```
    You should see `Hello World` printed.
    If you instead get errors about missing DLL files, run `source activate` first.
    The Jou compiler depends on DLLs in `mingw64\bin`,
    so `mingw64\bin` must be in PATH when running it.
8. Run tests:
    ```
    ./runtests.sh
    ```

</details>

After making changes to the compiler,
run `mingw32-make` (Windows) or `make` (other systems) to recompile the compiler.


## How does the compiler work?

See [doc/compiler_internals/architecture-and-design.md](doc/compiler_internals/architecture-and-design.md).


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
$ ./runtests.sh succ    # run tests/should_succeed/*, useful as a fast sanity check
$ ./runtests.sh aoc     # run Advent of Code solutions
$ ./runtests.sh class   # run tests related to defining classes
$ ./runtests.sh ascii   # run tests for the "stdlib/ascii.jou" module
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


## Releases

Using Jou on Linux is easy,
because you can install Jou's dependencies with `apt` (or whatever package manager you have).
To make using Jou on Windows equally easy, or at least as painless as possible,
users can download a zip file that contains everything needed to run Jou.
See the README for instructions to install Jou from a zip file.

A zip file is built and released in GitHub Actions every morning at 4AM UTC,
or when triggered manually from GitHub's UI
(e.g. to release an important bugfix as quickly as possible).

Sometimes a pull request doesn't affect Jou users in any way, but makes things easier for Jou developers.
These pull requests should be marked with the `skip-release` label in GitHub.
No new release is made if there are no new commits or they all have the `skip-release` label.

Some parts of the build are done in `.github/workflows/windows.yml`,
and the rest is in `release.sh` (invoked from `.github/workflows/release.yml`).
The difference is that `windows.yml` builds and tests the Windows zip file,
and `release.sh` creates a GitHub release for it.

There is also `jou --update`.
On Windows it runs a PowerShell script that downloads and installs the latest release from GitHub Actions.
On other platforms it simply runs `git pull` and recompiles the Jou compiler.
