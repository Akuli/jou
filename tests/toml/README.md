# TOML Test Suite

This directory contains everything needed to test Jou's `stdlib/toml.jou`
with [the `toml-test` program](https://github.com/toml-lang/toml-test/).
To get started, run:

```
$ ./run.sh
```

Currently only x86_64 Linux is supported.


## How it works

The following programs are involved in this:
- `run.sh` is a script that manages everything.
- `toml-test` is an executable that is downloaded from GitHub.
    You don't need to download anything manually.
    Just run `./run.sh` and it will do the download for you.
- `parser_program` (compiled from `parser_program.jou`)
    is a Jou program that `toml-test` invokes.
    It attempts to parse the given TOML documents using `stdlib/toml.jou`.
    It reads a TOML document from stdin.
    If it thinks that the TOML is valid, it prints a JSON representation of it
    [in the format that `toml-test` expects](https://github.com/toml-lang/toml-test/?tab=readme-ov-file#json-encoding).
    If it thinks that the TOML is invalid,
    it prints an error message and exits with status 1.

Here's what `./run.sh` does:
1. Downloads `toml-test` from GitHub
2. Compiles `parser_program.jou` to `./parser_program`
3. Invokes `toml-test` and passes `./parser_program` and `expected_errors.toml` to it as command-line arguments

Because `toml-test` runs the TOML parser in a subprocess,
it can be used to test TOML parsers written in different programming languages.
So we basically just plug a Jou program into an existing tool.

See the comments in `expected_errors.toml` for more info about it.


## Exploring the tests

The TOML documents that `toml-test` feeds to `parser_program` are baked into the `toml-test` executable.
If you need to explore them, you can extract them to a directory like this:

```
$ ./toml-test copy -toml 1.1 name_of_output_directory
```


## TOML versions

The Jou TOML parser targets TOML 1.1, not TOML 1.0.
It is important to **specify `-toml 1.1`** when you invoke `toml-test`.
[Due to a bug](https://github.com/toml-lang/toml-test/issues/188),
omitting the `-toml` flag or using `-toml latest` causes `toml-test` to use TOML 1.0 tests.
To be clear, **`-toml latest` is broken** and does not mean the latest TOML version.
