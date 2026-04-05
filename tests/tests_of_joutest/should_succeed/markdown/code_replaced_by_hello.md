# A weird test case

The following code block runs `echo hello` instead of the Jou compiler.
As such, any "code" in it should be ignored, and the output is always hello.

```jou
Blah blah blah.
This can be really whatever.

The important thing here is:
# Output: hello
```

If this works correctly, `joutest` silently ignores this,
just like a redirection like `command < file.txt` would do.

Here is an even bigger test with about 10kB of text to ensure this doesn't depend on buffering.
That said, I don't expect this to catch most problems on POSIX,
because the default buffer size is quite big, at least on linux.

```jou
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.

# Output: hello

Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
Blah blah blah. A lot of text here. Or is it code? I guess so. Anyway, there's a lot here.
```
