This file explains how to develop the Jou compiler.


## Setup

<details> <summary>Linux</summary>

Following the [instructions in the README](README.md#setup-linux) is enough.

To edit the C code, you can use any editor that uses `clangd`.
The `make` command creates a file `compile_flags.txt`
to help `clangd` find the LLVM header files.

</details>
    
<details> <summary>64-bit Windows</summary>

1. Download and install Git from [Git's website](https://git-scm.com/download/win) if you don't have it already.
1. To install LLVM, download and run LLVM's GitHub releases:
    [LLVM-13.0.1-win64.exe from here](https://github.com/llvm/llvm-project/releases/tag/llvmorg-13.0.1), or
    [LLVM-11.1.0-win64.exe from here](https://github.com/llvm/llvm-project/releases/tag/llvmorg-11.1.0).
    Check the "Add LLVM to the system PATH for all users" checkbox during the installation.
2. Download and install codeblocks from one of the download sites linked on
    [their official website](http://www.codeblocks.org/downloads/binaries/#imagesoswindows48pnglogo-microsoft-windows).
    Make sure to download a version that comes with mingw,
    such as `codeblocks-20.03mingw-setup.exe`.
    In the setup, the "Standard" installation contains everything you need.
4. Add `C:\Program Files\CodeBlocks\MinGW\bin` to the `PATH` environment variable through Control Panel.
    Without this, CodeBlocks doesn't find `clang`, the C compiler that LLVM comes with that is used to compile the Jou compiler.
    Let me know if you need more detailed instructions for this step.
5. Clone the project with the command prompt:
    ```
    cd Desktop
    git clone https://github.com/Akuli/jou
    ```
    You can put the project anywhere. The above command places it on the desktop.
6. Open the `jou` folder that you cloned with Git.
7. Right-click `llvm_headers.zip` and extract it.
    You should end up with a folder named `llvm_headers` inside the `jou` folder.
8. Start CodeBlocks. It will probably ask you what should be the default C compiler.
    This doesn't really matter because Jou comes with configuration that overrides the default anyway.
8. Open the CodeBlocks project (`jou.cbp` in the `jou` folder) with CodeBlocks.
10. Click the build button (yellow gear) at top of CodeBlocks.
    If everything succeeds, this creates `jou.exe`.
    If something goes wrong, please create an issue on GitHub.
11. Run a Jou program:
    ```
    cd Desktop\jou
    jou.exe examples\hello.jou
    ```
    You should see `Hello World` printed.

If CodeBlocks won't start and complains about a missing file `api-ms-win-crt-string-l1-1-0.dll`,
make sure that LLVM is installed and you remembered to add it to `PATH`.
LLVM conveniently comes with a DLL file that CodeBlocks developers apparently forgot to include.

CodeBlocks doesn't have a dark theme by default.
You can install a dark theme from e.g. [https://github.com/virtualmanu/Codeblocks-Themes](https://github.com/virtualmanu/Codeblocks-Themes).

</details>


## How does the compiler work?

The compiler is currently written in C. At a high level, the compilation steps are:
- Tokenize: split the source code into tokens
- Parse: build an Abstract Syntax Tree from the tokens
- Typecheck: errors for wrong number or type of function arguments etc, figure out the type of each expression in the AST
- Build CFG: build Control Flow Graphs for each function from the AST
- Simplify CFG: simplify and analyze the control flow graphs in various ways, emit warnings as needed
- Codegen: convert the CFGs into LLVM IR
- Invoke `clang` and pass it the generated LLVM IR

To get a good idea of how these steps work,
you can look at what the compiler produces in each compilation step:

```
$ ./jou --verbose examples/hello.jou
```

This shows the tokens, AST, CFGs and LLVM IR generated.
The control flow graphs are shown twice, before and after simplifying them.

After exploring the verbose output, you should probably
read `src/jou_compiler.h` and have a quick look at `src/util.h`.


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

This command does a few things:
- If not on Windows, it compiles the Jou compiler if you have changed something in `src/` since the last time it was compiled. (On Windows you need to use the build button in CodeBlocks to compile.)
- It runs all Jou files in `examples/` and `tests/`
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
    and `make valgrind` would take several minutes if they weren't skipped.
- Most problems in error message code are spotted by non-valgrinded tests.

There are also a few other ways to run the tests.
You can look at `.github/workflows/` to see how the CI runs tests.

Sometimes the fuzzer discovers a bug that hasn't been caught with tests.
It mostly finds bugs in the tokenizer,
because the fuzzer works by feeding random bytes to the compiler.

```
$ ./fuzzer.sh
```


## Windows Release Builds

Using Jou on Linux is easy,
because you can install Jou's dependencies with `apt` (or whatever package manager you have).
To make using Jou on Windows equally easy, or at least as painless as possible,
users can download a zip file that contains everything needed to run Jou.
See the README for instructions to install Jou from a zip file.

A zip file is built and released in GitHub Actions every 3 hours,
or when triggered manually from GitHub's UI
(e.g. to release an important bugfix as quickly as possible).

Some parts of the build are done in `.github/workflows/windows.yml`,
and the rest is in `.github/workflows/release.yml`.
The difference is that `windows.yml` runs on every pull request and on every push to `main`,
but `release.yml` runs every 3 hours and when triggered manually.
This means that:
- `windows.yml` should do most of the build.
    It should also run tests on the build results to make sure that everything works.
- `release.yml` should release whatever `windows.yml` created.
    Releasing-specific logic belongs here.
    For example, `release.yml` generates a name for the release based on the current UTC time.
