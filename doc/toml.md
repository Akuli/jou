# TOML Parsing

TL;DR:

```python
import "stdlib/toml.jou"
import "stdlib/io.jou"
import "stdlib/list.jou"

def main() -> int:
    toml = parse_toml("\
    message = 'hello'\n\
    do_the_thing = true\n\
    number1 = 12\n\
    number2 = 34.56\n\
    \n\
    [nested.thing]\n\
    value = 789\n\
    \n\
    [[array]]\n\
    x = 1\n\
    \n\
    [[array]]\n\
    x = 2\n\
    ")

    # Checking if parsing succeeded
    if toml.type == TOMLType.Error:
        printf("error on line %d: %s\n", toml.lineno, toml.error_message)
        return 1

    # Debug print
    # Output: Table on line 1:
    # Output:   message = String on line 1: "hello"
    # Output:   do_the_thing = Boolean on line 2: true
    # Output:   number1 = Integer on line 3: 12
    # Output:   number2 = Floating on line 4: 34.56
    # Output:   nested = Table on line 6:
    # Output:     thing = Table on line 6:
    # Output:       value = Integer on line 7: 789
    # Output:   array = Array on line 9:
    # Output:     Table on line 9:
    # Output:       x = Integer on line 10: 1
    # Output:     Table on line 12:
    # Output:       x = Integer on line 13: 2
    toml.print()

    # Getting a string
    message = toml_to_string(toml_get(&toml, "message"))
    if message != NULL:
        printf("it says %s\n", message)  # Output: it says hello

    # Getting a boolean
    if toml_is_true(toml_get(&toml, "do_the_thing")):
        printf("let's do the thing\n")  # Output: let's do the thing
    if toml_is_false(toml_get(&toml, "do_the_thing")):
        printf("let's not do the thing\n")

    # Getting a number as int
    printf("%d\n", toml_to_int(toml_get(&toml, "number1"), -1))  # Output: 12
    printf("%d\n", toml_to_int(toml_get(&toml, "number2"), -1))  # Output: -1

    # Getting a number as double
    printf("%f\n", toml_to_double(toml_get(&toml, "number1")))  # Output: 12.000000
    printf("%f\n", toml_to_double(toml_get(&toml, "number2")))  # Output: 34.560000

    # Getting a nested value
    # Output: 789
    printf("%d\n", toml_to_int(toml_get(toml_get(toml_get(&toml, "nested"), "thing"), "value"), -1))

    # Looping through an array
    # Output: 1
    # Output: 2
    toml_array = toml_get(&toml, "array")
    if toml_array != NULL and toml_array.type == TOMLType.Array:
        for p = toml_array.array.ptr; p < toml_array.array.end(); p++:
            printf("%d\n", toml_to_int(toml_get(p, "x"), -1))

    # Free memory used by the TOML object
    toml.free()

    return 0
```

Functions:

| Function                          | Return type   | Return value                                      |
|-----------------------------------|---------------|---------------------------------------------------|
| `parse_toml(string)`              | `TOML`        | `.type` is `TOMLType.Table` or `TOMLType.Error`   |
| `toml_get(toml, key)`             | `TOML*`       | pointer into value in `toml` or NULL              |
| `toml_to_int(toml, fallback)`     | `int`         | value in `toml` or the given `fallback`           |
| `toml_to_int64(toml, fallback)`   | `int64`       | value in `toml` or the given `fallback`           |
| `toml_to_double(toml)`            | `double`      | value in `toml` or NaN                            |
| `toml_to_string(toml)`            | `byte*`       | string in `toml` or NULL                          |
| `toml_is_true(toml)`              | `bool`        | `True` if `toml` is a TOML `true`                 |
| `toml_is_false(toml)`             | `bool`        | `True` if `toml` is a TOML `false`                |


## Overview

Jou's `stdlib/toml.jou` is a TOML 1.1 parser that doesn't support TOML dates and times.
Here's what that means:
- There is nothing in `stdlib/toml.jou` to generate TOML; it can only parse existing TOML.
    This is usually not a problem, because TOML files are usually written by humans, though not always
    (e.g. Rust's `cargo.lock`).
- You can parse TOML 1.0 and 1.1 files with `stdlib/toml.jou`.
    However, if `stdlib/toml.jou` successfully parses a TOML file,
    it doesn't mean that any other TOML parser can also parse it,
    because many other TOML parsers still use TOML 1.0.
- TOML supports dates and times natively.
    For example, `vacation_day = 2026-03-10` is valid syntax in TOML.
    If you attempt to parse a TOML file contains a date or a time with Jou's TOML parser,
    it will return an error saying that dates and times are not supported.

Please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new)
if you run into any of these limitations.

The `TOML` class represents a value inside the TOML.
Here's how it's defined, with a lot of details omitted for now
(see [the section about accessing directly below](#accessing-the-data-structures-directly)
if you're curious):

```python
@public
enum TOMLType:
    Error
    Table
    Array
    String
    Boolean
    Integer
    Floating

@public
class TOML:
    type: TOMLType
    union:
        error_message: byte[300]    # only for TOMLType.Error
        table: List[TOMLKeyVal]     # only for TOMLType.Table
        array: List[TOML]           # only for TOMLType.Array
        ...
    ...
    lineno: int     # Line number, available on all TOML objects
    ...
```

Parsing TOML with `stdlib/toml.jou` always goes something like this:
1. Call the `parse_toml()` function. It returns a `TOML` object. Let's call it `toml` for now.
2. Check if parsing succeeded.
    - If `toml.type == TOMLType.Table`, it means that parsing succeeded.
        In TOML, a "table" means a collection of key-value pairs,
        and successfully parsing a TOML file always produces such a collection.
    - If `toml.type == TOMLType.Error`, it means that parsing failed.
        You can handle the error in whatever way you want,
        perhaps using `.error_message` and `.lineno`.
    - Other members of the `TOMLType` [enum](enums.md) are not used for the returned `TOML` object.
3. Access whatever you need from the `toml` object.
4. Call `toml.free()` to free memory used in the TOML parsing.
    This is similar to [`free(list.ptr)` for lists](lists.md#what-does-freelistptr-do).
    You don't need to call `toml.free()` when parsing failed, but doing so is harmless.

The rest of this documentation focuses on accessing what you need from the TOML object (step 3 above).

At any point, if you are wondering what a `TOML` object contains,
there's a `.print()` method that you can call:

```python
import "stdlib/toml.jou"
import "stdlib/io.jou"

def main() -> int:
    toml = parse_toml("x = 123")

    # Output: Table on line 1:
    # Output:   x = Integer on line 1: 123
    toml.print()

    toml.free()
    return 0
```


## Finding values in TOML

The `toml_get(toml: TOML*, key: byte*) -> TOML*` function
finds the value corresponding to `key` in a TOML table `toml`.
It returns `NULL` if there is no such key.
For example:

```python
import "stdlib/toml.jou"
import "stdlib/io.jou"

def main() -> int:
    toml = parse_toml("foo = 123")

    # Output: found foo
    foo = toml_get(&toml, "foo")
    if foo != NULL:
        printf("found foo\n")
    else:
        printf("foo not found\n")

    # Output: bar not found
    bar = toml_get(&toml, "bar")
    if bar != NULL:
        printf("found bar\n")
    else:
        printf("bar not found\n")

    toml.free()
    return 0
```

The `toml_get()` function also returns `NULL` if `toml` is not a table,
or if either `toml` or `key` is `NULL`.
This means that if you nest multiple calls to `toml_get()`,
the result is `NULL` if any of them fails:

```python
import "stdlib/toml.jou"
import "stdlib/io.jou"

def main() -> int:
    toml = parse_toml("[foo.bar]\nthing = 123")

    foo_bar_thing = toml_get(toml_get(toml_get(&toml, "foo"), "bar"), "thing")
    if foo_bar_thing != NULL:
        printf("found it\n")  # Output: found it
    else:
        printf("not found\n")

    foo_lolwat_thing = toml_get(toml_get(toml_get(&toml, "foo"), "lolwat"), "thing")
    if foo_lolwat_thing != NULL:
        printf("found it\n")
    else:
        printf("not found\n")  # Output: not found

    toml.free()
    return 0
```

This also means that if all you don't care about showing a nice error message,
you might not need to check whether `parse_toml()` failed:

```python
import "stdlib/toml.jou"
import "stdlib/io.jou"

def main() -> int:
    toml = parse_toml("this is not valid toml syntax")
    if toml_get(&toml, "foo") == NULL:
        printf("not found\n")   # Output: not found
    toml.free()
    return 0
```

Once you have found the `TOML` object you want,
you can use these functions to access its underlying value:

- `toml_is_true(toml: TOML*) -> bool` returns `True` if `toml` is a `true` TOML value.
- `toml_is_false(toml: TOML*) -> bool` returns `True` if `toml` is a `false` TOML value.
- `toml_to_int(toml: TOML*, fallback: int) -> int` gets an `int` from a TOML number.
    This also works if the value is a TOML float that represents an integer, such as `123.0`.
    If `toml` does not represent an integer value that [fits into a Jou `int`](types.md#integers),
    then this function returns the given `fallback`.
    Usually `fallback` should be either a reasonable default value or something like `-1`.
    See also [the notes about numbers below](#notes-about-numbers).
- `toml_to_int64(toml: TOML*, fallback: int64) -> int64` is exactly what you would expect:
    a 64-bit version of `toml_to_int()`.
- `toml_to_double(toml: TOML*) -> double` gets a `double` value from a TOML value.
    This also works if the value is a TOML integer or infinity.
    If `toml` doesn't represent a number, this function returns NaN.
    To check for NaN, you can use the `isnan()` function in [stdlib/math.jou](../stdlib/math.jou).
    See also [the notes about numbers below](#notes-about-numbers).
- `toml_to_string(toml: TOML*) -> byte*` gets a string from a TOML value.
    If `toml` is not a TOML string, or the TOML string contains a zero byte,
    this function returns NULL.
    Do not `free()` the returned string yourself,
    and do not access it after calling the `.free()` method on the TOML object returned from `parse_toml()`.
    Basically, the TOML object owns the returned string.
    Use `strdup(toml_to_string(...))` or similar
    if you need to keep the string around longer than the TOML objects.
    The `strdup()` function is declared in [stdlib/str.jou](../stdlib/str.jou).
    See also [the notes about strings below](#notes-about-strings).

If `toml` is `NULL`, these functions handle it just like any other invalid input.
This means that you can do e.g. `string = toml_to_string(toml_get(...))`,
and if anything goes wrong, then `string` becomes `NULL`.


## Accessing the data structures directly

Sometimes it's easier to access the contents of a `TOML` object directly
instead of using the functions documented above.
**This is currently the only way to access TOML arrays.**

Here's how the `TOML` class is defined in [stdlib/toml.jou](../stdlib/toml.jou):

```python
@public
enum TOMLType:
    Error
    Table
    Array
    String
    Boolean
    Integer
    Floating

@public
class TOML:
    type: TOMLType

    # Only one of these is used depending on the type!
    union:
        error_message: byte[300]    # only for TOMLType.Error
        table: List[TOMLKeyVal]     # only for TOMLType.Table
        array: List[TOML]           # only for TOMLType.Array
        string: byte*               # only for TOMLType.String
        boolean: bool               # only for TOMLType.Boolean
        integer: int64              # only for TOMLType.Integer
        floating: double            # only for TOMLType.Floating

    # For TOMLType.String, this is the string length with included zero bytes.
    # For other types this is always zero.
    string_len: intnative

    lineno: int     # Line number (starts from 1), available on all TOML objects
    depth: int      # 0 for return value of parse_toml(), 1 for its contents, etc

@public
class TOMLKeyVal:
    key: byte*
    value: TOML
```

For example, to check whether the TOML contains a float somewhere,
you can do this:

```python
import "stdlib/toml.jou"
import "stdlib/io.jou"
import "stdlib/list.jou"

def contains_float(toml: TOML*) -> bool:
    match toml.type:
        case TOMLType.Error | TOMLType.String | TOMLType.Boolean | TOMLType.Integer:
            return False
        case TOMLType.Floating:
            return True
        case TOMLType.Table:
            # Recursively check the value of each key-value pair
            for kv = toml.table.ptr; kv < toml.table.end(); kv++:
                if contains_float(&kv.value):
                    return True
            return False
        case TOMLType.Array:
            # Recursively check each array item
            for p = toml.array.ptr; p < toml.array.end(); p++:
                if contains_float(p):
                    return True
            return False

def main() -> int:
    toml = parse_toml("[foo]\ndeeply.nested.array = [1, 2, 3.4]\n")
    if contains_float(&toml):
        printf("There is a float\n")  # Output: There is a float
    toml.free()
    return 0
```

If you do this, here are some things to be aware of:
- You may need to check for `NULL` before you access `.type` or any other fields.
- Do not access members of the [union](keywords.md#union)
    that are meant to be used with a different `TOMLType`.

For example, here's how to print each string in a TOML array
without crashing the program regardless of what the TOML contains:

```python
import "stdlib/toml.jou"
import "stdlib/io.jou"
import "stdlib/list.jou"

def main() -> int:
    toml = parse_toml("array = ['foo', 'bar', 123123, 'baz']")
    toml_array = toml_get(&toml, "array")

    if toml_array != NULL and toml_array.type == TOMLType.Array:  # <--- important!
        # Output: foo
        # Output: bar
        # Output: Skipping bad value
        # Output: baz
        for p = toml_array.array.ptr; p < toml_array.array.end(); p++:
            string = toml_to_string(p)
            if string == NULL:
                puts("Skipping bad value")
            else:
                puts(string)

    toml.free()
    return 0
```


## Notes about numbers

If you set the locale with C's `setlocale()` function, that may confuse `toml.jou`.
For example, in the Finnish language,
the preferred way to write a number like 12.34 is with a comma, as in 12,34.
So, on my Finnish system, if I call C's `setlocale()` function like `setlocale(LC_ALL, "fi_FI.UTF-8")`,
then attempting to parse a `12.34` in TOML produces `12.0` instead of `12.34`,
because `toml.jou` calls the C `atof()` function to parse the number,
and `atof()` does not understand the `.34` part of the number because it's expecting `,34`.
Some libraries (e.g. Gtk) call `setlocale()` automatically,
and that can also cause this problem.
Please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new) if you run into this.
There is [a similar problem with JSON](json-parsing.md#notes-about-numbers).

If any integer in the TOML doesn't fit into [a Jou `int64`](types.md#integers),
then `parse_toml()` fails by returning an error with message `integer doesn't fit to int64`.


## Notes about strings

TOML strings can contain zero bytes anywhere in the string,
but [Jou strings use the zero byte to mark the end](tutorial.md#more-about-strings).
For example, the TOML string like `"foo\x00bar"` would appear as just `"foo"` in Jou.
Here's how the TOML parser handles this:
- In a `TOML` object, the string is always stored with an extra zero byte at the end,
    and the actual length in bytes is stored to the `.string_len` field.
- The `toml_to_string()` function returns `NULL` for strings that contain a zero byte.

For example:

```python
import "stdlib/toml.jou"
import "stdlib/io.jou"

def main() -> int:
    toml = parse_toml("weird_string = \"foo\\x00bar\"")

    # Accessing with toml_to_string() function
    s = toml_to_string(toml_get(&toml, "weird_string"))
    if s == NULL:
        printf("got nothing\n")  # Output: got nothing
    else:
        printf("got %s\n", s)

    # Accessing the data structure directly
    obj = toml_get(&toml, "weird_string")
    if obj != NULL and obj.type == TOMLType.String:
        # Output: got foo (7 bytes)
        printf("got %s (%d bytes)\n", obj.string, obj.string_len)

        # Output: fooZEROBYTEbar
        for i = 0; i < obj.string_len; i++:
            if obj.string[i] == '\0':
                printf("ZEROBYTE")
            else:
                putchar(obj.string[i])
        putchar('\n')

    toml.free()
    return 0
```

It is an error if a key (as opposed to a value) contains a zero byte:

```python
import "stdlib/toml.jou"
import "stdlib/io.jou"

def main() -> int:
    toml = parse_toml("[stuff]\n\"weird\\x00key\" = 123")
    if toml.type == TOMLType.Error:
        # Output: error on line 2: zero bytes in keys are not supported
        printf("error on line %d: %s\n", toml.lineno, toml.error_message)

    toml.free()
    return 0
```
