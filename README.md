# Jou programming language

Jou is an experimental toy programming language. It looks like this:

```python3
import "stdlib/io.jou"

def main() -> int:
    puts("Hello World")
    return 0
```

See the [examples](./examples/) and [tests](./tests/) directories for more example programs.

Goals:
- Minimalistic feel of C + simple Python-style syntax
- Possible target audiences:
    - People who find C programming fun
    - Python programmers who want to try programming at a lower level (maybe to eventually learn C or Rust)
- Compatibility with C, not just as one more feature but as the recommended way to do many things
- Self-hosted compiler
- Eliminate some stupid things in C. For example:
    - Many useful warnings being disabled by default
    - UB for comparing pointers into different memory areas
        (as in `array <= foo && foo < array+sizeof(array)/sizeof(array[0])`)
    - `negative % positive` is negative or zero, should IMO be positive or zero
        (unless that is a lot slower, of course)
    - Strict aliasing
    - `int` possibly being only 16 bits
    - `long` possibly being only 32 bits
    - `char` possibly being more than 8 bits
    - `char` possibly being signed
    - `char` being named `char` even though it's really a byte
- Generics, so that you can implement a generic `list` (dynamically growing array)
    better than in C
- Compiler errors for most common bugs in C (missing `free()`, double `free()`, use after free, etc.)
- More keywords (`def`, `decl`, `forwarddecl`)
- Enumerated unions = C `union` together with a C `enum` to tell which union member is active
- Windows support that doesn't suck

Non-goals:
- Yet another big language that doesn't feel at all like C (C++, Zig, Rust, ...)
- Garbage collection (should feel lower level than that)
- Wrapper functions for the C standard library
- Wrapper libraries for existing C libraries (should just use the C library directly)
- Trying to detect every possible memory bug at compile time
    (Rust already does it better than I can, and even then it can be painful to use)
- Copying Python's gotchas
    (e.g. complicated import system with weird syntax and much more weird runtime behavior)


## Setup

These instructions are for using Jou.
The instructions for developing Jou are in [CONTRIBUTING.md](CONTRIBUTING.md).

<details> <summary>Linux</summary>

1. Install the dependencies:
    ```
    $ sudo apt install git llvm-13-dev clang-13 make
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

It is also possible to use llvm and clang version 11 instead of 13.
By default, the `make` command decides automatically
whether to use LLVM and clang version 11 or 13,
preferring version 13 if it is installed.
You can also specify the version manually by setting the `LLVM_CONFIG` variable:

```
$ sudo apt install llvm-11-dev clang-11
$ make clean    # Delete files that were compiled with previous LLVM version
$ LLVM_CONFIG=llvm-config-11 make
```

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
