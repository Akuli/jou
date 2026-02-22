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


## Finding values from JSON

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

If you need to loop through all items in a JSON array.
- `json_array_first(json: byte*) -> byte*` finds the first item in a JSON array.
    If `json` doesn't start with a JSON array, this function returns `NULL`.
- `json_array_next(json: byte*) -> byte*` takes a pointer to a value inside a JSON array
    and returns a pointer to the next value.
    If `json` is `NULL`, it doesn't point at a JSON value, or it points at the last JSON value of an array,
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

    return 0
```


## Notes about numbers

`Infinity`, `-Infinity` and `NaN` in JSON are fully supported.
They are not in the JSON spec, but many tools and libraries support them to some extent, so we support them too.

If you set the locale with C's `setlocale()` function, that may confuse `json.jou`.
For example, in the Finnish language,
the preferred way to write a number like 12.34 is with a comma, as in 12,34.
So, on my Finnish system, if I call C's `setlocale()` function like `setlocale(LC_ALL, "fi_FI.UTF-8")`,
then attempting to parse `12.34` in JSON produces `12.0`, not `12.34`,
because to get `12.34` the JSON string would need to contain `12,34` with a comma.
Some libraries (e.g. Gtk) call `setlocale()` automatically,
and that can also cause this problem.
Please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new) if you run into this.


## Notes about strings

It is currently not possible to add a string containing the zero byte `\0` to JSON.
This would be easy to implement if needed, so
please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new) if you need this.

The string given to `JSONBuilder.string()` should be valid UTF-8.
If it isn't, the resulting JSON will simply contain the given invalid UTF-8.

The `JSONBuilder.string()` method places most [non-ASCII characters](tutorial.md#characters) to the JSON as is.
The only two exceptions are U+2028 and U+2029 (`"\xe2\x80\xa8"` and `"\xe2\x80\xa9"` in UTF-8),
which become `\u2028` and `\u2029` in JSON.
These characters are valid in JSON, but they sometimes cause problems
because they are not valid in JavaScript code.
For example:

```python
import "stdlib/json.jou"
import "stdlib/io.jou"
import "stdlib/mem.jou"

def main() -> int:
    jb = JSONBuilder{}
    jb.begin_object()
    jb.key("€möji")
    jb.string("😀")
    jb.key("funny chars")
    jb.string("\xe2\x80\xa8 and \xe2\x80\xa9")
    jb.end_object()

    json = jb.finish()
    puts(json)  # Output: {"€möji":"😀","funny chars":"\u2028 and \u2029"}
    free(json)
    return 0
```

In JSON, the forward slash can be escaped, as in `"https:\/\/example.com"` or `"<\/script>"`.
The JSON builder does not escape forward slashes, but that would be easy to implement.
Please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new)
if you need to create JSON with escaped forward slashes.


## Notes about arrays and objects

In this context, an "object" means a JSON object, such as `{"a":1,"b":2}`.

No more than 64 levels of nested arrays and objects are supported.

The JSON builder uses `assert` statements to catch some common bugs:
- calls to `.begin_array()` and `.end_array()` must match
- calls to `.begin_object()` and `.end_object()` must match
- `.key()` must be called once before each value inside an object
- multiple values cannot be created without placing them into an array or an object
- arrays and objects cannot be nested more than 64 levels deep

For example, if you forget to call `.end_object()`,
you might get an error message that looks something like this:

```
Assertion 'self.depth == 0' failed in file "/some/path/to/jou/stdlib/json.jou", line 123.
```

When this happens, check your `end_array()` and `end_object()` calls.
It's very easy to get them wrong.

The JSON builder does not check whether you use the same key multiple times
in the same JSON object, as in `{"a":1,"a":2}`.
Please don't do that.
JSON is supposed to work consistently with many different programming languages,
and most JSON parsers load JSON objects into a data structure
that does not allow multiple values for the same key, e.g. Python's `dict`.
