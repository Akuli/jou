# Jou programming language

Jou is a programming language that **looks like Python but behaves like C**.
For example:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("Hello World\n")

    # Print numbers 0 to 9
    for i = 0; i < 10; i++:
        printf("%d\n", i)

    return 0
```

See the [examples](./examples/) and [tests](./tests/) directories for more example programs
or read [the Jou tutorial](./doc/tutorial.md).

For now, Jou is great for writing small programs that don't have a lot of dependencies.
For example, I solved all problems of [Advent of Code 2023](https://adventofcode.com/2023/) in Jou,
and I'm currently working on Advent of Code 2024.
See [examples/aoc2023](./examples/aoc2023/)
and [examples/aoc2024](./examples/aoc2024/) for the code.

I think Jou will be useful for two kinds of people:
- People who find C programming fun but like Python's syntax
- Python programmers who want to try programming at a lower level (maybe to eventually learn C or Rust)


## Design goals and non-goals

Jou eliminates some surprising things in C. For example:
- In C, `char` may or may not be signed, depending on your OS,
    but Jou's `byte` data type is always unsigned.
- In C, `negative % positive` is negative or zero,
    which means that `array[i % array_len]` doesn't wrap around as expected.
    In Jou, `negative % positive` is positive or zero.
- Jou doesn't have strict aliasing.
    This means that in Jou, memory is just bytes,
    and you can't get [UB](doc/ub.md) by interpreting the same bytes in different ways,
    like you can in C.
- Jou has Windows support that doesn't suck.
    You simply download and extract a zip, and add it to `PATH`.
    (See instructions below.)

I will try my best to **keep Jou simple**,
and not turn it into yet another big language that doesn't feel like C,
such as C++, Zig, Rust, and many others.
For example, the recommended way to print things will be C's `printf()` function,
as explained in [the Jou tutorial](./doc/tutorial.md#cs-standard-library-libc).
This also means that I reject many feature requests.

Jou is not intended to be memory safe, because it would make Jou more difficult to use.
See [Jou's UB documentation](./doc/ub.md) for more discussion,
including [thoughts on Rust](./doc/ub.md#rusts-approach-to-ub).


## Setup

These instructions are for using Jou.
The instructions for developing Jou are in [CONTRIBUTING.md](CONTRIBUTING.md).

<details> <summary>Linux</summary>

1. Install the dependencies:
    ```
    $ sudo apt install git llvm-14-dev clang-14 make
    ```
    Let me know if you use a distro that doesn't have `apt`,
    and you need help with this step.
2. Download and compile Jou.
    ```
    $ git clone https://github.com/Akuli/jou
    $ cd jou
    $ make
    ```
3. Run the hello world program to make sure that Jou works:
    ```
    $ ./jou examples/hello.jou
    Hello World
    ```
    You can now run other Jou programs in the same way.
4. (Optional) If you want to run Jou programs with simply `jou filename`
    instead of something like `./jou filename` or `/full/path/to/jou filename`,
    you can add the `jou` directory to your PATH.
    To do so, edit `~/.bashrc` (or whatever other file you have instead, e.g. `~/.zshrc`):
    ```
    $ nano ~/.bashrc
    ```
    Add the following line to the end:
    ```
    export PATH="$PATH:/home/yourname/jou/"
    ```
    Replace `/home/yourname/jou/` with the path to the folder (not the executable file) where you downloaded Jou.
    Note that the `~` character does not work here,
    so you need to use a full path (or `$HOME`) instead.

These LLVM/clang versions are supported:
- LLVM 11 with clang 11
- LLVM 13 with clang 13
- LLVM 14 with clang 14

By default, the `make` command picks the latest available version.
You can also specify the version manually by setting the `LLVM_CONFIG` variable:

```
$ sudo apt install llvm-11-dev clang-11
$ make clean    # Delete files that were compiled with previous LLVM version
$ LLVM_CONFIG=llvm-config-11 make
```

</details>

<details> <summary>MacOS</summary>

MacOS support is new. Please create an issue if something doesn't work.

1. Install Git, make and LLVM 13.
    If you do software development on MacOS, you probably already have Git and make,
    because they come with Xcode Command Line Tools.
    You can use [brew](https://brew.sh/) to install LLVM 13:
    ```
    $ brew install llvm@13
    ```
2. Download and compile Jou.
    ```
    $ git clone https://github.com/Akuli/jou
    $ cd jou
    $ make
    ```
3. Run the hello world program to make sure that Jou works:
    ```
    $ ./jou examples/hello.jou
    Hello World
    ```
    You can now run other Jou programs in the same way.
4. (Optional) If you want to run Jou programs with simply `jou filename`
    instead of something like `./jou filename` or `/full/path/to/jou filename`,
    you can add the `jou` directory to your PATH.
    To do so, edit `~/.bashrc` (or whatever other file you have instead, e.g. `~/.zshrc`):
    ```
    $ nano ~/.bashrc
    ```
    Add the following line to the end:
    ```
    export PATH="$PATH:/Users/yourname/jou/"
    ```
    Replace `/Users/yourname/jou/` with the path to the folder (not the executable file) where you downloaded Jou.
    Note that the `~` character does not work here,
    so you need to use a full path (or `$HOME`) instead.

</details>

<details> <summary>NetBSD</summary>
Support for NetBSD is still experimental. Please report bugs and
shortcomings.

1. Install the dependencies:
    ```
    # pkgin install bash clang git gmake libLLVM
    ```
    Optionally `diffutils` can be installed for coloured diff outputs.
2. Download and compile Jou.
    ```
    $ git clone https://github.com/Akuli/jou
    $ cd jou
    $ gmake
    ```
3. Run the hello world program to make sure that Jou works:
    ```
    $ ./jou examples/hello.jou
    Hello World
    ```
    You can now run other Jou programs in the same way.
4. (Optional) If you want to run Jou programs with simply `jou
    filename` instead of something like `./jou filename` or
    `/full/path/to/jou filename`, you can add the `jou` directory to
    your PATH.  Refer to the manual page of your login shell for exact
    syntax.

NB: Using Clang and LLVM libraries built as a part of the base system
is not currently supported.

</details>

<details> <summary>64-bit Windows</summary>

1. Go to releases on GitHub. It's in the sidebar at right.
2. Choose a release (latest is probably good) and download a `.zip` file whose name starts with `jou_windows_64bit_`.
3. Extract the zip file somewhere on your computer.
4. You should now have a folder that contains `jou.exe`, lots of `.dll` files, and subfolders named `stdlib` and `mingw64`.
    Add this folder to `PATH`.
    If you don't know how to add a folder to `PATH`,
    you can e.g. search "windows add to path" on youtube.
5. Write Jou code into a file and run `jou filename.jou` on a command prompt.
    Try [the hello world program](examples/hello.jou), for example.

</details>


## Updating to the latest version of Jou

Run `jou --update`.
On old versions of Jou that don't have `--update`,
you need to instead delete the folder where you installed Jou
and go through the setup instructions above again.


## Editor support

Tell your editor to syntax-highlight `.jou` files as if they were Python files.
You may want to copy some other Python settings too,
such as how to handle indentations and comments.

If your editor uses a langserver for Python,
make sure it doesn't use the same langserver for Jou.
For example, vscode uses the Pylance language server,
and you need to disable it for `.jou` files;
otherwise you get lots of warnings whenever you edit
Jou code that would be invalid as Python code.

For example, I use the following configuration with the
[Porcupine](https://github.com/Akuli/porcupine) editor:

```toml
[Jou]
filename_patterns = ["*.jou"]
pygments_lexer = "pygments.lexers.Python3Lexer"
syntax_highlighter = "pygments"
comment_prefix = '#'
autoindent_regexes = {dedent = 'return( .+)?|break|pass|continue', indent = '.*:'}
```

To apply this configuration, copy/paste it to end of Porcupine's `filetypes.toml`
(menubar at top --> *Settings* --> *Config Files* --> *Edit filetypes.toml*).


## How does the compiler work?

See [CONTRIBUTING.md](CONTRIBUTING.md).
