Default settings will look something like:

```toml
verbose = false
parallel = true

[defaults_for_all_tests]
run_compiler_under_valgrind = false
markdown.languages_to_test_as_jou = ["jou"]
timeout_seconds = 60
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

# This would be nice, but let's not do it, doesn't interact nicely with anything else:

# tests/foo.jou: ....ok
# bar.jou (working directory: tests): .........ok
# tests/weird.jou: skip
# tests/bad.jou: FAIL
# tests/too_slow.jou: timed out after 60 seconds
