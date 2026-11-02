# Parsing command-line arguments

The `main` function receives the argument count and argument vector as `argc`
and `argv`. `argv[0]` is the program name; the command-line arguments start at
`argv[1]`.

## Parsing arguments manually

For a small program, you can loop over `argv` and use `match ... with strcmp`
to check each argument:

```python
import "stdlib/io.jou"
import "stdlib/str.jou"

def main(argc: int, argv: byte**) -> int:
    verbosity = 0
    for i = 1; i < argc; i++:
        match argv[i] with strcmp:
            case "--help":
                printf("Usage: %s [-v | --verbose]\n", argv[0])
                return 0
            case "-v" | "--verbose":
                verbosity++
            case _:
                fprintf(get_stderr(), "%s: unknown argument \"%s\"\n", argv[0], argv[i])
                return 2

    printf("verbosity = %d\n", verbosity)
    return 0
```

This approach is straightforward, but each argument is handled as one
complete string. It does not recognize `--output=file` as `--output` plus a
value, or split combined short flags like `-vvv` into three `-v` flags.
You would need to implement those cases yourself.

## Using `stdlib/argparse.jou`

`ArgumentParser` handles long options with `=`, combined short flags, and
values supplied after a flag. It yields one flag at a time; the program still
decides which flags are valid and which flags take values.

```python
import "stdlib/argparse.jou"
import "stdlib/assert.jou"
import "stdlib/io.jou"
import "stdlib/str.jou"


def main(argc: int, argv: byte**) -> int:
    verbosity = 0
    infile: byte* = NULL
    outfile: byte* = NULL
    debug_dump_file: byte* = NULL

    parser = ArgumentParser{argv = argv}
    while parser.next():
        match parser.flag with strcmp:
            case "--help":
                printf("Usage: %s [options] [INPUT_FILE]\n", argv[0])
                printf("\n")
                printf("Options:\n")
                printf("  -v/--verbose        print more information (can be repeated)\n")
                printf("  -o FILE             output file\n")
                printf("  --debug-dump FILE   dump debug stuff to file\n")
                return 0
            case "-v" | "--verbose":
                verbosity++
            case "-o":
                value = parser.take_value()
                if value == NULL:
                    fprintf(get_stderr(), "%s: you must specify a file name after -o (try --help)\n", argv[0])
                    return 2
                outfile = value
            case "--debug-dump":
                value = parser.take_value()
                if value == NULL:
                    fprintf(get_stderr(), "%s: you must specify a file name after --debug-dump (try --help)\n", argv[0])
                    return 2
                debug_dump_file = value
            case "":
                if infile != NULL:
                    fprintf(get_stderr(), "%s: only one input file can be given (try --help)\n", argv[0])
                    return 2
                infile = parser.take_value()
                assert infile != NULL
            case _:
                fprintf(get_stderr(), "%s: unknown argument \"%s\" (try --help)\n", argv[0], parser.flag)
                return 2

        if parser.pending_value != NULL:
            fprintf(get_stderr(), "%s: flag \"%s\" does not take a value (try --help)\n", argv[0], parser.flag)
            return 2

    printf("verbosity = %d\n", verbosity)

    if infile == NULL:
        printf("infile = NULL\n")
    else:
        printf("infile = \"%s\"\n", infile)

    if outfile == NULL:
        printf("outfile = NULL\n")
    else:
        printf("outfile = \"%s\"\n", outfile)

    if debug_dump_file == NULL:
        printf("debug_dump_file = NULL\n")
    else:
        printf("debug_dump_file = \"%s\"\n", debug_dump_file)

    return 0
```

## Quirks and caveats

- Initialize the parser with the argument vector: `ArgumentParser{argv = argv}`.
  The parser starts at `argv[1]`, leaving the program name in `argv[0]`.
- `parser.next()` sets `parser.flag` to the option being processed. For a
  positional argument, `parser.flag` is the empty string and the argument is
  available through `parser.take_value()`.
- A value written with `=` is initially in `parser.pending_value`. Call
  `take_value()` to consume it, or check `pending_value` after handling the
  flag to reject values on flags that do not accept them. `next()` clears any
  unconsumed pending value, so forgetting to check it silently discards input
  such as `--verbose=yes`.
- For a value-taking flag, `take_value()` accepts `-o FILE`, `-o=FILE`,
  `-oFILE`, and `--output=FILE`. For long options, a space-separated value
  such as `--output FILE` also works. An empty value such as `--output=` is
  not `NULL`; check for `NULL` to detect a missing value.
- A separate value beginning with `-` is normally left for the next iteration.
  The parser does accept `-`, `--`, and negative numbers such as `-1` as
  values. `--` is not treated as an end-of-options marker; it is a positional
  argument.
- Short flags can be combined: `-vvv` yields three `-v` flags, and
  `-voFILE` yields `-v` followed by `-o` with value `FILE`. The parser does
  not know which flags take values, so your program must call `take_value()`
  for the appropriate flag.
- The flag buffer holds at most 99 bytes plus its terminating NUL. Keep flag
  names within that limit.
