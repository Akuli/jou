Default settings will look something like:

```toml
verbose = false  # TODO: not implemented
parallel = true  # TODO: not implemented

[defaults_for_all_tests]
run_compiler_under_valgrind = false
markdown.languages_to_test_as_jou = ["jou"]
timeout_seconds = 60  # TODO: not implemented
stdout = "compare_to_comments"
stderr = "compare_to_comments"
cd_to_containing_directory = false
skip = false
```

Output will look like:

```
ok:  jou tests/foo.jou
ok:  jou bar.jou (working directory: tests)
skip jou tests/weird.jou
FAIL jou tests/bad.jou
FAIL jou tests/too_slow.jou (timed out after 60 seconds)
```

This would be nice, but let's not do it, doesn't interact nicely with anything else:

```
tests/foo.jou: ....ok
bar.jou (working directory: tests): .........ok
tests/weird.jou: skip
tests/bad.jou: FAIL
tests/too_slow.jou: timed out after 60 seconds
```

Processes are always invoked so that a `jou` installed in same directory with `joutest` is found:
- POSIX: insert directory containing `joutest` to start of `$PATH`
    - finds `jou` before anything else in PATH
- Windows: call `CreateProcessA` (later `CreateProcessW`) with `lpApplicationName` set to NULL and `lpCommandLine` like `"jou file.jou"`
    - finds `jou.exe` before looking in PATH
    - need to implement the notorious CRT quoting rules: https://copilot.microsoft.com/shares/SKCRV4ZCxTGuP71dGLt79

High level structure:
1. discover tests
    - do the globs
    - figure out which configurations apply to each file
    - do not apply the configurations yet!!!
2. configure tests
    - walk through TOML
3. gather expected outputs
    - read files and parse for comments
4. run tests
5. report results
