# JSON

TL;DR: Parsing JSON is not supported. Example of building JSON:

```python
import "stdlib/json.jou"

import "stdlib/io.jou"
import "stdlib/mem.jou"


def main() -> int:
    jb = JSONBuilder{pretty_print = 2}

    jb.object()  # {
    # "foo": 123
    jb.key("foo")
    jb.number(123)
    # "bar": ["hello", 12.34, true, null]
    jb.key("bar")
    jb.array()  # [
    jb.string("hello")
    jb.number(12.34)
    jb.boolean(True)
    jb.null()
    jb.end_array()  # ]
    jb.end_object()  # }

    json = jb.finish()

    # Output: {
    # Output:   "foo": 123,
    # Output:   "bar": [
    # Output:     "hello",
    # Output:     12.34,
    # Output:     true,
    # Output:     null
    # Output:   ]
    # Output: }
    puts(json)

    # Do this when you no longer need the returned string
    free(json)

    return 0
```


## Building JSON

The `JSONBuilder` class is used to create (that is, build) strings of JSON.
It is meant to be used in the following way:

```python
jb = JSONBuilder{}
...  # call methods of jb to actually build the JSON
json = jb.finish()
...  # use json (it's a string)
free(json)
```

You should initialize all fields of `JSONBuilder` that you don't use to zero
by e.g. using [the `ClassName{}` syntax](classes.md#instantiating-syntax) as shown above.
Those other fields are not documented here because you rarely need to care about them.

When you create a JSONBuilder, you can set the following fields:
- `pretty_print: int` field to decide how whitespace will be added.
    If `pretty_print` is zero, the JSON will be created without any unnecessary whitespace.
    If `pretty_print` is positive, the JSON will be indented with that many spaces.

The `.finish()` method returns [a string](tutorial.md#more-about-strings) of JSON.
You must `free()` the JSON string, [just like with lists](lists.md#what-does-free-list-ptr-do),
because the builder internally uses a `List[byte]` to construct the string and then returns its `.ptr`.

To actually build the JSON, use the following methods, where `jb` is a `JSONBuilder`:
- `jb.boolean(b: bool)` adds `true` or `false` to the JSON.
- `jb.null()` adds `null` to the JSON.
- `jb.string(s: byte*)` adds a string to the JSON. If `s` is `NULL`, it instead adds `null` just like `jb.null()` would.
- `jb.number(n: double)` adds a number to the JSON. See also [the section on numbers](#numbers).
- `jb.array()` and `jb.end_array()` are used to build a JSON array.
    Between calling these methods, you build each item of the array.
- `jb.object()`, `jb.end_object()` and `jb.key(key: byte*)` are used to build a JSON object.
    In JSON, an object looks like `{"key1": value1, "key2": value2}`.
    Between `jb.object()` and `jb.end_object()`,
    you must call `jb.key(some_string)` before you build each value.


## Numbers

This section documents a few surprising things and potential problems with how `json.jou` handles numbers.
**Please create an issue on GitHub if you run into these limitations.**
It is possible to make many of these things more configurable than they are now.

`Infinity`, `-Infinity` and `NaN` in JSON are fully supported.
They are not in the JSON spec, but many tools and libraries support them to some extent, so we support them too.
For example:

```python
import "stdlib/json.jou"
import "stdlib/io.jou"
import "stdlib/mem.jou"

def main() -> int:
    jb = JSONBuilder{}
    jb.array()
    jb.number(1.0 / 0.0)
    jb.number(-1.0 / 0.0)
    jb.number(0.0 / 0.0)
    jb.end_array()
    json = jb.finish()
    puts(json)  # Output: [Infinity,-Infinity,NaN]
    free(json)
    return 0
```

The `.number()` method uses `snprintf()` to format the number, and that depends on the current locale.
If you (or a library in your project, e.g. Gtk) sets the locale with C's `setlocale()` function,
that may confuse `json.jou`.
For example, in the Finnish language, the preferred way to write a number like 12.34 is with a comma, as in 12,34.
On my Finnish system, after a `setlocale(LC_ALL, "fi_FI.UTF-8")`,
calling `jb.number(12.34)` produces `12,34` in the JSON instead of `12.34`, which is wrong.
Please create an issue on GitHub if you run into this problem.
