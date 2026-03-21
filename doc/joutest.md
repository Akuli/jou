# The `joutest` test runner

Jou comes with a test runner named `joutest`.
It is currently very incomplete, and more features will be added soon,
but it is already usable for some Jou projects.


## Getting started

The most common way to test a Jou project is to
just put files named `test_foo.jou` into a `tests` directory and run each one with Jou.
The test files can [import other files](imports.md) of your project
with imports like `import "../src/thing.jou"`.
For example, the tests of [my curses-klondike project](https://github.com/Akuli/curses-klondike)
are like this.

To use `joutest` this way, create a file `joutest.toml` that contains just these two lines:

```toml
[[tests]]
files = "tests/test_*.jou"
```

Now you can simply run `joutest` inside your project directory.
It should run the tests:

```
akuli@Akuli-Desktop ~/curses-klondike $ joutest
...

3 succeeded
```

Each dot means that a file ran successfully.
You can also try `--verbose` (or `-v`, that does the same thing):

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


## Output Comments

By default, `joutest` checks that your test files print exactly what they are supposed to print.
It determines the expected output from for comments like `# Output: foo` in the file being tested.
The space after `:` is required, but as a special case,
`# Output:` at the end of a line means an empty line of output.

For example, the following test passes:

```python
import "stdlib/io.jou"

def main() -> int:
    # Output: Hello
    # Output: World
    printf("Hello\nWorld\n")   

    # Empty line
    printf("\n")    # Output:

    printf("Bla1\n")    # Output: Bla1
    printf("Bla2\n")    # Output: Bla2
    printf("Bla3\n")    # Output: Bla3
    printf("Bla4\n")    # Output: Bla4
    printf("Bla5\n")    # Output: Bla5

    return 0
```

If the expected and actual output do not match, `joutest` shows a diff.
For example, changing `printf("Bla4\n")` to `printf("Baa4\n")` above produces the following output:

```diff
F

------- FAILURES -------

*** Command: jou test.jou ***
@@ 3 lines not shown @@
 Bla1
 Bla2
 Bla3
-Bla4
+Baa4
 Bla5


0 succeeded, 1 failed
```

So red `-` lines are the expected output, and green `+` lines are the actual output.
Note that this is backwards from "red is bad and green is good" thinking,
but consistent with the "red was changed to green" coloring that is very common with diffs.


## Condition Tables

Let's say that for whatever reason, you have separate test files
for Windows and for other operating systems.
Here's how you would set that up in `joutest.toml`:

```toml
[[tests]]
files = {windows = "windows_tests/test_*.jou", default = "posix_tests/test_*.jou"}
```

In `joutest.toml`, **any TOML table with a `default` key is a condition table.**
Before `joutest` looks up any settings from `joutest.toml`,
it replaces each condition table with one of its values.
So on Windows, the above is equivalent to:

```toml
[[tests]]
files = "windows_tests/test_*.jou"
```

The following keys can be used in condition tables:

| Key           | When is the value used?                           |
|---------------|---------------------------------------------------|
| `windows`     | `joutest` is running on Windows                   |
| `macos`       | `joutest` is running on MacOS                     |
| `linux`       | `joutest` is running on Linux                     |
| `32bit`       | `joutest` is running on a 32-bit operating system |
| `default`     | nothing else matches                              |

If multiple different keys match,
`joutest` will fail with an error and refuse to run any tests.
For example, the following is probably a bad idea,
because it does not work at all on 32-bit Linux systems:

```toml
files = {linux = "linux_tests.jou", 32bit = "32bit_tests.jou", default = "fallback_tests.jou"}
```

You can instead use nested condition tables to explicitly define
which value is considered more important:

```toml
# This uses linux_tests.jou on 32-bit linux
files = {
    linux = "linux_tests.jou",
    default = {
        32bit = "32bit_tests.jou",
        default = "fallback_tests.jou",
    },
}

# This uses 32bit_tests.jou on 32-bit linux
files = {
    32bit = "32bit_tests.jou",
    default = {
        linux = "linux_tests.jou",
        default = "fallback_tests.jou",
    },
}
```


## Content of `joutest.toml`

Note that any value can be specified as a condition table (see above).

The only required things are the `tests` array, and `files` inside each table of the `tests` array.
Everything else is optional.

- `tests` (required) is an array of one or more tables with the following keys:
    - `files` (required) is a glob string or an array of one or more glob strings.
        The supported glob features are `*` (match zero or more characters within a path component)
        and `**` (match zero or more entire path components).
        For example, `files = ["**/*.md"]` finds all markdown files,
        including any markdown files in the same directory with `joutest.toml`.
    - More will be added in the future...
- `defaults_for_all_tests` is just like each table of the `tests` array,
    except that you cannot specify `files`.
    As the name suggests, these settings are used
    when an item of the `tests` array does not specify something.

If the `tests` array contains multiple tables whose `files` glob matches the same file,
then values in the last matching table are preferred.
This way you can specify something general first and special cases afterwards.
For example:

```toml
# Run all example files
[[tests]]
files = "examples/*.jou"

# ...except this example file
[[tests]]
files = "examples/dont_do_this.jou"
skip = true   # TODO: this option doesn't exist yet, sorry :(
```


## Location of `joutest.toml`

The `joutest.toml` file must be in the same directory where you invoke `joutest`.
There is no hidden file finding logic: `joutest` literally does `fopen("joutest.toml", "rb")`,
and the content of `joutest.toml` specifies where to find the files to be tested.


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
command = ["jou", "{file}"]
run_compiler_under_valgrind = false
markdown.languages_to_test_as_jou = ["jou"]
timeout_seconds = 60
stdout = "compare_to_comments"
stderr = "compare_to_comments"
special_output_comments = {
    Output = "{comment}",
    # The following will be useful for developing the Jou compiler, but
    # probably won't be not enabled by default.
    #Error = 'compiler error in file "{file}", line {line}: {comment}',
    # This will be useful for testing joutest with itself.
    #Error = 'joutest error in file "{file}", line {line}: {message}',
}
cd_to_containing_directory = false
skip = false
```

The main thing to note here is `"compare_to_comments"` and `special_output_comments`.
This means that `joutest` will look for comments like `# Output: hello` in the file it's testing,
and ensure that `hello` is actually printed when running the file.

It will be possible to pass markdown files to `joutest`,
and `joutest` will extract code blocks from the markdown and run them as tests.
This is useful to ensure that your documentation stays up to date.

The Jou project itself already has scripts for doing these things,
and they will eventually be replaced by `joutest`:
- [`runtests.sh`](../runtests.sh) runs tests with `# Output:` comment handling
- [`doctest.sh`](../doctest.sh) runs tests in markdown files, also with `# Output:` comment handling

Plan and status:
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
