# TOML Test Suite

This directory contains scripts and configuration to test Jou's `stdlib/toml.jou`
with [the `toml-test` program](https://github.com/toml-lang/toml-test/).

`toml-test` is a command-line program that can be used to test any TOML parser.
We give it a parser, it feeds TOML to the parser, and it looks at what the parser does.
More specifically:
- We run `toml-test` so that we give a path to an executable as a command-line argument.
- `toml-test` runs the given executable many times, and each time it feeds TOML to its stdin.
- Our program attempts to parse the given TOML documents with `stdlib/toml.jou`.
    If parsing succeeds, it prints a JSON representation of the TOML.
    Otherwise it prints an error message and exits with 1.

Because `toml-test` runs the TOML parser in a subprocess,
it can be used to test TOML parsers written in different programming languages.
So we basically just plug a Jou program into an existing tool.

To get started, run:

```
$ ./run.sh
```

Currently only x86_64 Linux is supported.

Source files:
- `run.sh`: The main script that does everything.
- `parser_program.jou`: Wraps `stdlib/toml.jou` into a program that `toml-test` can call.
- `expected_errors.toml`: Defines the error messages that our TOML parser should emit.
    Given to `toml-test` as a command-line argument..

Generated files:
- `toml-test`: The executable created by TOML developers.
    The `run.sh` script automatically downloads this from GitHub releases if it doesn't exist yet.
- `parser_program`: The executable given to `toml-test`. Compiled from `toml_parser_program.jou`.
