# Syntax Specification

This page explains in detail how Jou's syntax works.
It is probably too detailed for most Jou users,
and it is meant to be useful for people who work on the Jou compiler
or who just want to know how it works.


## Source files

Jou files should be saved with the UTF-8 encoding, without the BOM mark.
Windows CRLF line endings (`\r\n`) and Linux/UNIX LF line endings (`\n`) are both accepted.
However, `\r` bytes can be used only in CRLF line endings:
it is an error if the file contains a `\r` byte that isn't immediately followed by `\n`.

It is also an error if a Jou source file contains tab characters or zero bytes.
They cannot occur even inside strings or byte literals.
Use spaces for indentation, and use backslashes to express zero bytes and tabs,
as in `'\0'` or `"foo\tbar"`.


## Tokenizing

Tokenizing is basically the first step in compiling a Jou file.
It converts the file's contents into a sequence of tokens.
For example, `print("hello")` on a line of its own turns into 5 tokens:
`print`, `(`, `"hello"`, `)`, and a newline token that marks the end of the line of code.

To see the tokens of a file, run the Jou compiler with `--tokenize-only`.
For example, to see the tokens of the hello world program,
run `jou --tokenize-only examples/hello.jou`.

Jou has a few different kinds of tokens:
- **Int literals** can be specified in base 10 (e.g. `123`), hex (`0x123abc` or `0x123ABC`), octal (`0o777`) or binary (`0b010101`).
    The prefixes `0x`, `0o` and `0b` are case-sensitive.
    A minus sign is never a part of an int literal token: `-10` tokenizes as two separate tokens.

    It is an error if the value of an int literal does not fit in a signed 32-bit integer (Jou `int`);
    that is, if its value is greater than or equal to 2<sup>31</sup>.

    Unnecessary zeros in the beginning are allowed in hex, octal and binary (so `0x000f` is fine), but are not allowed in base 10.
    This is because in C, `0123` is somewhat surprisingly same as `83`, because the extra `0` makes it an octal number.
    Jou uses an explicit `0o` prefix for octal numbers, similarly to Python.

- **Short literals** are just like in literals, except that they have an extra `S` at the end (e.g. `114S`),
    The `S` must be uppercase.

- **Long literals** are just like int literals, except that they have an extra `L` at the end (e.g. `123L`),
    and they only need to fit in a signed 64-bit integer (Jou `long`).
    The `L` must be uppercase.
- **Double literals** look like `12.` or `12.34` or `123.456e5` or `1e-5`.
    They consist of one or more digits (0-9), then `.`, then zero or more digits.
    If `e` or `e-` occurs after that (the `e` must be lowercase),
    after the `e` or `e-` there must be one or more digits.
    The number after `e` basically tells how much to shift the decimal point:
    `123.456e5` is same as `12345600.0` and `1e-5` is same as `0.00001`, for example.
- **Float literals** are just like double literals except that they have an extra `F` or `f` at the end.
- **Byte literals** (also known somewhat misleadingly as character literals)
    consist of a one-byte character placed between single quotes, as in `'a'`.
    Note that `'ö'` is an error, because the `ö` character is two bytes in UTF-8.
    The backslash character has a special meaning (see below for details).
- **String literals** are similar to byte literals,
    except that they use double quotes and they can contain any number of bytes.
- **Name tokens** consist of one or more letters A-Z or a-z, numbers 0-9 and underscores `_`.
    The first character of a name token cannot be a number.

    If the name token is in the keyword list below, it is not actually a name token
    and it turns into a keyword token instead.
    This means that keywords cannot be used in places that use a name token,
    such as variable names and function names.

- **Keyword tokens** are any of the following:
    - `import`
    - `def`
    - `declare`
    - `class`
    - `union`
    - `enum`
    - `global`
    - `return`
    - `if`
    - `elif`
    - `else`
    - `while`
    - `for`
    - `pass`
    - `break`
    - `continue`
    - `True`
    - `False`
    - `None`
    - `NULL`
    - `void`
    - `noreturn`
    - `and`
    - `or`
    - `not`
    - `self`
    - `as`
    - `sizeof`
    - `assert`
    - `bool`
    - `byte`
    - `short`
    - `int`
    - `long`
    - `float`
    - `double`
- **Newline tokens** occur at the end of a line of code.
    Lines that only contain spaces and comments do not produce a newline token;
    this ensures that blank lines are ignored as they should be.
- **Indent tokens** mean that the next line of code is more indented than the previous line:
    each indent token means that there is 4 spaces more indentation.
    Indent tokens always occur just after newline tokens.
    It is an error if the code is indented with tabs or with some indentation size other than 4 spaces.
- **Dedent tokens** are added whenever the amount of indentation decreases by 4 spaces.
- **Operator tokens** are any of the following: `... == != -> <= >= ++ -- += -= *= /= %= :: . , : ; = ( ) { } [ ] & % * / + - < >`
    Note that `a = = b` and `a == b` do different things:
    `a = = b` tokenizes as 4 tokens (and the parser errors when it sees the tokens)
    while `a == b` tokenizes as 3 tokens.

The backslash character has a special meaning in string literals (e.g. `"hello\n"`) and byte literals (e.g. `'\n'`):
- `\n` represents the newline byte (also known as the LF byte).
- `\r` represents the carriage return byte (also known as the CR byte).
- `\t` represents a tab character.
- `\` at the end of a line means that the string continues from the start of the next line.
    This cannot be used in byte literals.
- `\'` is the single quote byte.
    This is supported only in byte literals, not in strings,
    because you can simply type `'` as is into a string.
- `\"` is the double quote byte. This is supported only in strings.
- `\0` is the zero byte. It cannot be used inside a string, because in strings,
    it is a special byte that marks the end of the string.
- `\x` followed by two hexadecimal digits (0-9, A-F, a-f) specifies a byte in hexadecimal.
    For example, `\x6a` or `\x6A` is equivalent to `j`,
    because the ASCII value of the `j` character is 106,
    which is 6A in hexadecimal.
    Note that because `\x00` is equivalent to `\0`, it cannot be used inside strings.
- `\` followed by anything else is an error.

Before tokenizing, the compiler adds an imaginary newline character to the beginning of the file.
This seems a little weird at first, but it simplifies tokenizing,
because it is enough to handle indentations just after producing a newline token.

Spaces are ignored everywhere except in indentations (just after a newline token).

The tokenizer also ignores comments.
A comment starts with a `#` character that is anywhere except inside a string literal or a byte literal,
and continues until the end of the line.

When a `(`, `[` or `{` operator appears, the tokenizer enters **space-ignoring mode**.
In space-ignoring mode, all newline characters and spaces
that aren't inside string literals or byte literals are ignored.
This means that the code below does not get newline tokens or indent/dedent tokens
inside the function call:

```python
printf(
    "hello world %d %s\n",
    123 + 456,
    foobar(),
)
```

The tokenizer exits space-ignoring when a corresponding `)`, `]` or `}` occurs,
so in the above example, there will be a newline token after the last `)`.
Nested parentheses work as you would expect:
the `)` of `foobar()` doesn't make the tokenizer exit space-ignoring mode just yet,
because the `(` from `printf(` hasn't been closed.


## Parsing

TODO
