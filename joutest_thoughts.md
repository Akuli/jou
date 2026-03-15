Default settings will look something like:

```toml
verbose = false
parallel = true

[defaults_for_all_tests]
valgrind_compiler = false
markdown.languages_to_test_as_jou = ["jou"]
timeout_seconds = 60
parse_comments = true
capture_stdout = true
capture_stderr = true
cd_to_containing_directory = false
```


Output will look like:

```
ok:  jou tests/foo.jou
ok:  jou bar.jou (working directory: tests)
skip jou tests/weird.jou
FAIL jou tests/bad.jou
FAIL jou tests/too_slow.jou (timed out after 60 seconds)
```

# This would be nice, but let's not do it, doesn't interact nicely with anything else:

# tests/foo.jou: ....ok
# bar.jou (working directory: tests): .........ok
# tests/weird.jou: skip
# tests/bad.jou: FAIL
# tests/too_slow.jou: timed out after 60 seconds
