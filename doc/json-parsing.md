# JSON

This page documents parsing JSON.
See [json-building.md](json-building.md) if you want to create a string of JSON.

TL;DR:

```python
import "stdlib/json.jou"
import "stdlib/io.jou"
import "stdlib/mem.jou"

def main() -> int:
    json = "{\"number\": 123, \"nothing\": null, \"nope\": false, \"message\": \"hello\", \"array\": [1.2, 3.4, 5.6], \"object\": {\"a\":7, \"b\":8, \"c\":9}}"

    # Validating (may or may not be necessary depending on your use case)
    if json_is_valid(json):
        puts("is valid")  # Output: is valid

    # Getting a number
    printf("%f\n", json_to_double(json_get(json, "number")))  # Output: 123.000000

    # Getting null
    if json_is_null(json_get(json, "nothing")):
        puts("found the null")  # Output: found the null

    # Getting a boolean
    if json_is_true(json_get(json, "nope")):
        puts("this does not get printed")
    if json_is_false(json_get(json, "nope")):
        puts("this gets printed")  # Output: this gets printed

    # Getting a string
    message = json_to_string(json_get(json, "message"))
    puts(message)  # Output: hello
    free(message)

    # Looping through an array
    # Output: 1.200000
    # Output: 3.400000
    # Output: 5.600000
    for j = json_array_first(json_get(json, "array")); j != NULL; j = json_array_next(j):
        printf("%f\n", json_to_double(j))

    # Looping through an object
    # Output: a --> 7.000000
    # Output: b --> 8.000000
    # Output: c --> 9.000000
    for j = json_object_first(json_get(json, "object")); j != NULL; j = json_object_next(j):
        key = json_to_string(j)
        printf("%s --> %f\n", key, json_to_double(json_object_value(j)))
        free(key)

    return 0
```


## Finding values in JSON

Unlike most other JSON libraries, Jou's JSON library does **not**
create a new data structure in memory to represent your entire JSON and everything inside it.
Instead, it simply navigates the JSON by passing around [pointers to the string](tutorial.md#more-about-strings).

For example, let's say that you have a string of JSON that looks like `{"a": 1, "b": 2, "c": 3}`,
and you want to get the value of `2` from it.
The `json_get()` function returns a pointer to the `2`,
which appears as the string `2, "c": 3}` when printed.
Functions like `json_to_double()` extract values from there,
and ignore the rest of the JSON at the end of the string, if any.
For example:

```python
import "stdlib/json.jou"
import "stdlib/io.jou"

def main() -> int:
    json = "{\"a\": 12, \"b\": 34, \"c\": 56}"
    pointer_to_b = json_get(json, "b")
    if pointer_to_b == NULL:
        puts("not found")
    else:
        puts(pointer_to_b)                              # Output: 34, "c": 56}
        printf("%f\n", json_to_double(pointer_to_b))  # Output: 34.000000
    return 0
```

This approach seems weird at first, but it is simpler than
creating a recursive data structure to represent any JSON value.
That would be needed to allow passing around values of mixed types
in a dynamically typed language like Jou.

The `json_get()` function correctly understands the structure of JSON.
For example, `json_get()` never finds a key from a nested object.
Here an "object" means a JSON object, such as `{"a":1,"b":2}`.
In the example below, it returns `NULL` for not found:

```python
import "stdlib/json.jou"
import "stdlib/io.jou"

def main() -> int:
    json = "{\"a\": 1, \"nested\": {\"b\": 2, \"c\": 3}}"
    pointer_to_b = json_get(json, "b")
    if pointer_to_b == NULL:
        puts("not found")  # Output: not found
    else:
        puts(pointer_to_b)
        printf("%f\n", json_to_double(pointer_to_b))
    return 0
```

Once you have found the value you want, you can use these functions to parse it:

- `json_is_true(json: byte*) -> bool` returns `True` if `json` points at a `true` JSON value.
- `json_is_false(json: byte*) -> bool` returns `True` if `json` points at a `false` JSON value.
- `json_is_null(json: byte*) -> bool` returns `True` if `json` points at a `null` JSON value.
    Note that this checks for the JSON value `null`, not the Jou `NULL` pointer:
    `json_is_null("null")` returns `True` but `json_is_null(NULL)` returns `False`.
- `json_to_double(json: byte*) -> double` gets a `double` value from a JSON number.
    If `json` doesn't start with a JSON number, this function returns NaN.
    See also [the notes about numbers below](#notes-about-numbers).
- `json_to_string(json: byte*) -> byte*` parses a string from JSON
    and returns it as `\0`-terminated UTF-8 (that is, a typical Jou string).
    If `json` doesn't start with a valid string, this function returns `NULL`.
    If the return value is not `NULL`, it must be `free()`d when you no longer need it.
    See also [the notes about strings below](#notes-about-strings).


## Error handling

All JSON parsing functions handle `NULL` just like any other invalid input.
Many functions also return `NULL` when they encounter invalid input.
This means that calls to JSON parsing functions can be nested easily,
and if anything goes wrong, you just get some value that denotes an error.

For example, `json_get(NULL, key)` returns `NULL`,
so if you nest multiple calls to `json_get()`,
the result is `NULL` if any of them fails:

```python
import "stdlib/json.jou"
import "stdlib/io.jou"

def main() -> int:
    json = "{\"a\": {\"this is not b\": {\"c\": true}}}"

    if json_get(json_get(json_get(json, "a"), "b"), "c") == NULL:
        puts("not found")  # Output: not found

    if json_is_true(json_get(json_get(json_get(json, "a"), "b"), "c")):
        puts("yes")
    else:
        puts("no")  # Output: no

    return 0
```

On the other hand, if `json_get()` finds the value it's looking for,
it returns immediately and does not check the rest of the JSON,
so a bit surprisingly, the following works:

```python
import "stdlib/json.jou"
import "stdlib/io.jou"

def main() -> int:
    json = "{\"thing\": 123, this is totally not valid json at all!"
    printf("%f\n", json_to_double(json_get(json, "thing")))  # Output: 123.000000
    return 0
```

The `json_is_valid(json: byte*) -> bool` function checks whether a string is valid JSON.
You can call it before parsing:

```python
import "stdlib/json.jou"
import "stdlib/io.jou"

def main() -> int:
    json = "{\"thing\": 123, this is totally not valid json at all!"
    if json_is_valid(json):
        printf("%f\n", json_to_double(json_get(json, "thing")))
    else:
        printf("Invalid json\n")  # Output: Invalid json
    return 0
```


## Looping through JSON arrays and objects

To access the items of a JSON array, use these functions:
- `json_array_first(json: byte*) -> byte*` finds the first item in a JSON array.
    If `json` doesn't start with a JSON array, this function returns `NULL`.
- `json_array_next(json: byte*) -> byte*` takes a pointer to a value inside a JSON array
    and returns a pointer to the next value.
    If `json` doesn't point at a JSON value, or it points at the last JSON value of an array,
    this function returns `NULL`.

These functions work nicely with Jou's [`for` loop syntax](loops.md#for-loop):

```python
import "stdlib/json.jou"
import "stdlib/io.jou"

def main() -> int:
    json = "{\"numbers\": [1.2, 3.4, 5.6]}"

    # Output: 1.200000
    # Output: 3.400000
    # Output: 5.600000
    for j = json_array_first(json_get(json, "numbers")); j != NULL; j = json_array_next(j):
        printf("%f\n", json_to_double(j))

    # Get the third number
    # Output: 5.600000
    printf("%f\n", json_to_double(json_array_next(json_array_next(json_array_first(json_get(json, "numbers"))))))

    return 0
```

Looping through an object is similar,
but note that [the `json_get()` function](#finding-values-in-json) is often more convenient.
In this context, an "object" means a JSON object, such as `{"a":1,"b":2}`.
- `json_object_first(json: byte*) -> byte*` finds the first key in a JSON object.
    If `json` doesn't start with a JSON object, this function returns `NULL`.
- `json_object_next(json: byte*) -> byte*` takes a pointer to a key inside a JSON object
    and returns a pointer to the next key.
    If `json` doesn't point at a key, or it points at the last key of an object,
    this function returns `NULL`.
- `json_object_value(json: byte*) -> byte*` takes a pointer to a key inside a JSON object
    and returns a pointer to the corresponding value.
    If `json` doesn't point at a key, this function returns `NULL`.

For example:

```python
import "stdlib/json.jou"
import "stdlib/io.jou"
import "stdlib/mem.jou"

def main() -> int:
    json = "{\"a\": 1, \"b\": 2, \"c\": 3}"

    # Output: a --> 1.000000
    # Output: b --> 2.000000
    # Output: c --> 3.000000
    for item = json_object_first(json); item != NULL; item = json_object_next(item):
        key = json_to_string(item)
        printf("%s --> %f\n", key, json_to_double(json_object_value(item)))
        free(key)

    return 0
```


## Notes about numbers

`Infinity`, `-Infinity` and `NaN` in JSON are fully supported.
They are not in the JSON spec, but many tools and libraries support them to some extent, so we support them too.

If you set the locale with C's `setlocale()` function, that may confuse `json.jou`.
For example, in the Finnish language,
the preferred way to write a number like 12.34 is with a comma, as in 12,34.
So, on my Finnish system, if I call C's `setlocale()` function like `setlocale(LC_ALL, "fi_FI.UTF-8")`,
then parsing numbers in JSON does not work.
Some libraries (e.g. Gtk) call `setlocale()` automatically,
and that can also cause this problem.
Please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new) if you run into this.


## Notes about strings

Because [Jou strings use the zero byte to mark the end](tutorial.md#more-about-strings),
it is currently not possible to distinguish between the JSON strings `"foo"` and `"foo\u0000bar"`.
Please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new)
if this is a problem for you.

If a JSON string contains invalid UTF-8, or `\u` escapes that form invalid UTF-16,
it is considered as an invalid string:
`json_is_valid()` returns `False` for any JSON containing such strings,
`json_to_string()` returns `NULL`, and
functions like `json_get()` and `json_array_next()`
return `NULL` when they would need to move past these strings.


## Notes about arrays and objects

In this context, an "object" means a JSON object, such as `{"a":1,"b":2}`.

No more than **64 levels** of nested arrays and objects are supported.
JSON containing that much nesting is treated as invalid:
`json_is_valid()` return False, and
functions like `json_get()` and `json_array_next()`
return `NULL` when they would need to move past these objects.
Please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new)
if this is a problem for you.
It would be relatively easy to make this limit adjustable.
