# The `joutest` test runner

Jou comes with a test runner named `joutest`.
It is currently very incomplete, and more features will be added soon,
but it is already usable enough for small projects.


## Getting started

The most common way to test a Jou project is to
just put files named `test_foo.jou` into a `tests` folder and run each one with Jou.
The test files can [import other files] in your project normally,
with for example `import "../src/thing.jou"`.
For example, the tests of [my curses-klondike project](https://github.com/Akuli/curses-klondike)
are like this.

To use `joutest` this way, create a file `joutest.toml` that contains just these two lines:

```toml
[[tests]]
files = "tests/test_*.jou"
```

Now you can simply run `joutest` inside your project folder.
It should run your tests:

```
akuli@Akuli-Desktop ~/curses-klondike $ joutest
...

3 succeeded
```

Each dot means a file that ran successfully.
You can also use `--verbose` to see more details:

```
akuli@Akuli-Desktop ~/curses-klondike $ joutest --verbose
run: jou "tests/test_args.jou"
 ok  jou "tests/test_args.jou"
run: jou "tests/test_card.jou"
 ok  jou "tests/test_card.jou"
run: jou "tests/test_klondike.jou"
 ok  jou "tests/test_klondike.jou"


3 succeeded
```


## Location of `joutest.toml`

The `joutest.toml` file must be in the same directory where you invoke `joutest`.
On the one hand, this makes `joutest` simpler: it can literally do `fopen("joutest.toml", "rb")`.
But on the other hand, this means that if you `cd` to a subdirectory within your project,
you must `cd` back out of it before running the tests.
If you find this annoying, please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new).


## Unimplemented features

These will be documented better as they are implemented.
This section is here mostly to give people looking at `joutest`
an idea of what I intend it to become in the future.

An empty `joutest.toml` is going to be equivalent to something like the following:
(note that this does not specify what files to test, that must be explicitly given)

```toml
verbose = false
parallel = true

[defaults_for_all_tests]
run_compiler_under_valgrind = false
markdown.languages_to_test_as_jou = ["jou"]
timeout_seconds = 60  # TODO: not implemented
stdout = "compare_to_comments"
stderr = "compare_to_comments"
cd_to_containing_directory = false
skip = false
```

Processes will be always invoked so that a `jou` installed in same directory with `joutest` is found:
- POSIX: insert directory containing `joutest` to start of `$PATH`
    - finds `jou` before anything else in PATH
- Windows: call `CreateProcessA` (later `CreateProcessW`) with `lpApplicationName` set to NULL and `lpCommandLine` like `"jou file.jou"`
    - finds `jou.exe` before looking in PATH

Plan and status (completed things are marked as [DONE]):
1. parse arguments
    - [DONE] `-v` / `--verbose`
    - `-O0` / `-O1` / `-O2` / `-O3` (jou opt level)
    - `--valgrind`
    - `--jou-flags`
    - [DONE] `--no-colors`
    - test name filter
2. [DONE] parse joutest.toml
    - [DONE] eliminate condition tables
    - [DONE] don't validate everything or place into nice data structures here
3. discover tests
    - [DONE] do the globs
    - markdown files: find code block start/end byte offsets
    - [DONE] figure out which configurations apply to each test
    - [DONE] do not apply the configurations yet!!!
    - sort tests by:
        - [DONE] file name
        - start offset (needed for markdown, `qsort()` is not a stable sort)
    - in TOML, use:
        - [DONE] `files`
        - `markdown.languages_to_test_as_jou`
4. configure tests
    - walk through and apply each relevant TOML section
    - this is where most of the validation should happen
5. gather expected outputs
    - read files and parse for comments
    - markdown: must seek
6. run tests
    - pre-test command like `make`
        - TODO: how about `./runtests.sh --dont-run-make`?
    - Windows:
        - use `CreateProcessA` (maybe later `CreateProcessW`)
        - set `lpApplicationName` to NULL and `lpCommandLine` like `"jou file.jou"`,
            so that `joutest.exe` will always prefer a `jou.exe`
            in the same directory with `joutest.exe` over anything that might be in `%PATH%`
        - implement the notorious CRT quoting rules
            to construct the string of arguments that no shell will ever see
    - POSIX:
        - use `posix_spawnp()`
        - prepend dirname of joutest executable to `$PATH`,
            so that `joutest.exe` will always prefer a `jou` in the same directory with `joutest`
    - if configured, capture stdout/stderr
    - if configured, discard stdout/stderr
    - run in parallel
7. show results
    - show diffs (need that algorithm.......)
    - show how many succeeded and failed
    - [DONE] exit 0 or 1
