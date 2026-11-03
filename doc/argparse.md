# Parsing command-line arguments

Jou comes with [stdlib/argparse.jou](../stdlib/argparse.jou) for parsing command-line arguments.
But first, we look into parsing command-line arguments by hand to understand how the library works,
and why you would or wouldn't use it.


## Parsing command-line arguments manually

In Jou, there are two ways to define the `main()` function:
- Use `def main() -> int:` if you don't care about command-line arguments.
- Use `def main(argc: int, argv: byte**) -> int:` if you want to use command-line arguments.

For example:

```python
# This program comes with Jou: examples/argc_argv.jou
import "stdlib/io.jou"

def main(argc: int, argv: byte**) -> int:
    for i = 0; i < argc; i++:
        printf("Argument %d: %s\n", i, argv[i])
    return 0
```

Argument 0 is the program name.
To pass more arguments, pass the `-o` flag to the Jou compiler,
so that it places the compiled program to an executable file instead of running it,
and then run the executable separately.

```
akuli@Akuli-Desktop ~/jou $ jou examples/argc_argv.jou 
Argument 0: examples/jou_compiled/argc_argv/argc_argv

akuli@Akuli-Desktop ~/jou $ jou -o program examples/argc_argv.jou 

akuli@Akuli-Desktop ~/jou $ ./program
Argument 0: ./program

akuli@Akuli-Desktop ~/jou $ ./program hello world 123
Argument 0: ./program
Argument 1: hello
Argument 2: world
Argument 3: 123

akuli@Akuli-Desktop ~/jou $ ./program "hello world" 123
Argument 0: ./program
Argument 1: hello world
Argument 2: 123
```

On Windows, you may need `program.exe` instead of `./program`.

Here are some conventions that most command-line interfaces follow:
- Exit code 0 means success. Any other exit code means an error.
- Exit code 2 usually means that the command-line arguments are invalid.
    (This doesn't matter much as long as you don't use zero.)
- A plain `printf(...)` prints to stdout, but errors should be printed to stderr.
    This way errors are visible even if stdout is redirected to a file or another program.
- Errors printed to stderr should mention the name of the program that printed them.
    This helps with debugging scripts and pipelines that consist of multiple programs.
- An option named `--help` should briefly explain how to use the command-line interface.
- Single-letter flags look like e.g. `-v` or `-o`: first a dash, then a letter.
- Multi-letter flags look like `--verbose` or `--some-long-name`.
- In `--help` messages, square brackets mean "optional", and UPPERCASE means a placeholder.
    For example, `[-v/--verbose] FILE` means that
    you don't have to specify `-v` (or `--verbose`) for the program to run,
    but you must specify a file.

The example below implements a simple command-line interface that follows these conventions.
The program takes a file name, and optionally, `-v` or `--verbose` to make it print more stuff.
Jou's [`match ... with strcmp` statement](match.md#using-match--with-strcmp-to-match-strings)
is useful for handling each argument.

```python
# This program comes with Jou: examples/cli_manual.jou
import "stdlib/io.jou"
import "stdlib/str.jou"

def main(argc: int, argv: byte**) -> int:
    filename: byte* = NULL
    verbosity = 0

    for i = 1; i < argc; i++:
        match argv[i] with strcmp:
            case "--help":
                printf("Usage: %s [-v/--verbose] FILE\n", argv[0])
                printf("\n")
                printf("Options:\n")
                printf("  -v/--verbose    print more information, can be repeated to print even more\n")
                return 0
            case "-v" | "--verbose":
                verbosity++
            case _:
                if argv[i][0] == '-':
                    fprintf(get_stderr(), "%s: unknown argument \"%s\" (try --help)\n", argv[0], argv[i])
                    return 2
                if filename != NULL:
                    fprintf(get_stderr(), "%s: only one file name can be given (try --help)\n", argv[0], argv[i])
                    return 2
                filename = argv[i]

    if filename == NULL:
        fprintf(get_stderr(), "%s: missing file name (try --help)\n", argv[0], argv[i])
        return 2

    printf("File name: %s\n", filename)
    printf("Verbosity: %d\n", verbosity)
    return 0
```

Example usage:

```
akuli@Akuli-Desktop ~/jou $ jou -o program2 examples/cli_manual.jou 

akuli@Akuli-Desktop ~/jou $ ./program2 
./program2: missing file name (try --help)

akuli@Akuli-Desktop ~/jou $ ./program2 --help
Usage: ./program2 [-v/--verbose] file

Options:
  -v/--verbose    print more information, can be repeated to print even more

akuli@Akuli-Desktop ~/jou $ ./program2 hello.txt
File name: hello.txt
Verbosity: 0

akuli@Akuli-Desktop ~/jou $ ./program2 hello.txt --verbose
File name: hello.txt
Verbosity: 1

akuli@Akuli-Desktop ~/jou $ ./program2 hello.txt -v -v
File name: hello.txt
Verbosity: 2

akuli@Akuli-Desktop ~/jou $ ./program2 hello.txt --asdf
./program2: unknown argument "--asdf" (try --help)
```


## Using stdlib/argparse.jou

If an experienced command-line user sees `-v/--verbose` in a `--help` message,
and one `-v` didn't print enough information,
the next thing they will likely try is `-vv` for even more verbosity.
It should be treated the same as `-v -v`, but the above program fails to do so:

```
akuli@Akuli-Desktop ~/jou $ ./program2 -vv
./program2: unknown argument "-vv" (try --help)
```

Parsing arguments with values is also somewhat tricky to implement correctly.
For example, let's say you have an option like `-o/--output` that expects a file name after it.
In most command-line interfaces, it would be accepted in all of the following ways:

```
--output outfile.txt
--output=outfile.txt
-o outfile.txt
-o=outfile.txt
-ooutfile.txt
```

To handle these problems and more, Jou comes with a class named `ArgumentParser`,
defined in [stdlib/argparse.jou](../stdlib/argparse.jou).

Unlike many other argument parsers,
Jou's `ArgumentParser` parses the arguments and does nothing else.
It does not print error messages, exit the process when an error occurs, or auto-generate a `--help` option.
This means that you need to write slightly more code,
but `ArgumentParser` doesn't get in your way if:
- you want to customize the `--help` message
- you want to change some detail about the error messages
- you need to print all messages in a different language than English
- you can't use exit code 2 for some reason.

The example below uses `ArgumentParser` to implement a command-line interface
with `-o` and `-v/--verbose` options, similarly to the Jou compiler.

```python
# This program comes with Jou: examples/cli_argparse.jou
```

Example usage:

```
akuli@Akuli-Desktop ~/jou $ jou -o program3 examples/cli_argparse.jou 

akuli@Akuli-Desktop ~/jou $ ./program3 
./program3: missing input file name (try --help)

akuli@Akuli-Desktop ~/jou $ ./program3 --help
Usage: ./program3 [-v/--verbose] [-o FILE] FILE

  -v/--verbose    print more information, can be repeated to print even more
  -o FILE         output an executable file, don't run the code

akuli@Akuli-Desktop ~/jou $ ./program3 hello.txt
Input file: hello.txt
Output file: NULL
Verbosity: 0

akuli@Akuli-Desktop ~/jou $ ./program3 hello.txt --verbose
Input file: hello.txt
Output file: NULL
Verbosity: 1

akuli@Akuli-Desktop ~/jou $ ./program3 hello.txt --verbose=whatever
./program3: flag "--verbose" does not take a value (try --help)

akuli@Akuli-Desktop ~/jou $ ./program3 hello.txt -vv -o hello2.out
Input file: hello.txt
Output file: hello2.out
Verbosity: 2

akuli@Akuli-Desktop ~/jou $ ./program3 -vvvo hello2.out hello.txt
Input file: hello.txt
Output file: hello2.out
Verbosity: 3

akuli@Akuli-Desktop ~/jou $ ./program3 -vvvo hello2.out hello.txt
Input file: hello.txt
Output file: hello2.out
Verbosity: 3
```


## The `ArgumentParser` class

`ArgumentParser` is the only [public](imports.md#public) thing in [stdlib/argparse.jou](../stdlib/argparse.jou).

Fields:
- `argv: byte**` is an array of strings. The first item (program name) is always skipped.
    There must be a `NULL` after the last item, and there always is:
    that's how `ArgumentParser` figures out where the arguments end without requiring `argc`.
    This is the only field that should be initialized to something other than zero
    when the `ArgumentParser` is created.
- `flag: byte[100]` is either a full command-line flag, or a part of it.
    For example, `-o=file.txt` would appear as a single string in `argv`,
    but `flag` would be set to just `"-o"`.
    Empty string means a plain old argument that has no special meaning.
    Very long flag names will be truncated to fit the array.
- `pending_value: byte*` should be used to detect unnecessary values,
    such as `--verbose=whatever` instead of just `--verbose` in the above example.
    When `next()` sees a flag with `=`, it sets `pending_value` to the part after `=`,
    so that `take_value()` can return `pending_value` and reset it back to `NULL`.
- A couple internal fields that most users of `ArgumentParser` don't need to care about

Methods:
- `next() -> bool` parses one argument. Return value `True` means that an argument was parsed, and `False` means end of argv.
- `take_value() -> byte*` is the difference between `--foo` and `--foo=bar`.
    Call `take_value()` once between the `next()` calls to take a value with the flag, as in `--foo=bar`.
    If no value was given, `take_value()` returns `NULL`.
