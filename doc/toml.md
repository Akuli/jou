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
        printf("error on line %d: %s\n", toml.lineno, toml.string)
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
    # Output: -1
    printf("%d\n", toml_to_int(toml_get(toml_get(toml_get(&toml, "asdasd"), "thing"), "value"), -1))

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
| `toml_is_false(toml)`             | `bool`        | `True` if `json` is a TOML `false`                |

Data structures:

```python
@public
class TOMLKeyVal:
    key: byte*
    value: TOML

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
        table: List[TOMLKeyVal]     # only for TOMLType.Table
        array: List[TOML]           # only for TOMLType.Array
        string: byte*               # only for TOMLType.String
        error_message: byte[300]    # only for TOMLType.Error
        boolean: bool               # only for TOMLType.Boolean
        integer: int64              # only for TOMLType.Integer
        floating: double            # only for TOMLType.Floating

    # For TOMLType.String, this is the string length with included zero bytes.
    # For other types this is always zero.
    string_len: intnative

    lineno: int     # Line number, available on all TOML objects
    depth: int      # 0 for return value of parse_toml(), 1 for its contents etc
```


## Overview

Parsing TOML with `stdlib/toml.jou` should always be done like this:
1. Call the `parse_toml()` function. It returns a `TOML` object.
2. Check if parsing succeeded.
    - If parsing succeeded, the `.type` field of the returned `TOML` object is `TOMLType.Table`.
        In TOML, a "table" means a collection of key-value pairs,
        and successfully parsing a TOML file always produces such a collection.
    - If parsing failed, the `.type` field of the returned `TOML` object is `TOMLType.Error`.
        Handle the error in whatever way you want.
        You may find `.error_message` and `.lineno` useful for this.
3. Access whatever you need from the `TOML` object.
4. Call the `.free()` method on the `TOML` object to free memory used by it.
    This is similar to [`free(list.ptr)` for lists](lists.md#what-does-freelistptr-do).
    You don't need to call the `.free()` method when parsing failed, but doing so is harmless.

The rest of this documentation focuses on accessing what you need from the TOML object (step 3).


## Simple interface: functions to get values

TODO: document the rest
