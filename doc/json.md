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
- `jb.number(n: double)` adds a number to the JSON. See also [the section on numbers below](#notes-about-numbers).
- `jb.array()` and `jb.end_array()` are used to build a JSON array.
    Between calling these methods, you build each item of the array.
    See also [the section on arrays and objects below](#notes-about-arrays-and-objects).
- `jb.object()`, `jb.end_object()` and `jb.key(key: byte*)` are used to build a JSON object.
    In JSON, an object looks like `{"key1": value1, "key2": value2}`.
    Between `jb.object()` and `jb.end_object()`,
    you must call `jb.key(some_string)` before you build each value.
    See also [the section on arrays and objects below](#notes-about-arrays-and-objects).


## Notes about numbers

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

Please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new)
if you want to disable the support for `Infinity`, `-Infinity` and `NaN`.

If you set the locale with C's `setlocale()` function, that may confuse `json.jou`.
For example, in the Finnish language,
the preferred way to write a number like 12.34 is with a comma, as in 12,34.
So, on my Finnish system, if I call C's `setlocale()` function like `setlocale(LC_ALL, "fi_FI.UTF-8")`,
and then I do `jb.number(12.34)`,
I get `12,34` in the JSON instead of the expected `12.34`.
Some libraries (e.g. Gtk) call `setlocale()` automatically,
and that can also cause this problem.
Please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new) if you run into this.


## Notes about strings

It is currently not possible to add a string containing the zero byte `\0` to JSON.
This would be easy to implement if needed, so
please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new) if you need this.

The `JSONBuilder.string()` method assumes that the string given to it is valid UTF-8.

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
    jb.object()
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
The JSON builder does not escape forward slashes.


## Notes about arrays and objects

In this context, an "object" means a JSON object, such as `{"a":1,"b":2}`.

No more than 64 levels of nested arrays and objects are currently supported.

The JSON builder uses `assert` statements to catch some common bugs:
- calls to `.array()` and `.end_array()` must match
- calls to `.object()` and `.end_object()` must match
- `.key()` must be called once before each value inside an object
- multiple values cannot be created without placing them into an array or an object
- arrays and objects cannot be nested more than 64 levels deep

For example, if you forget to call `.end_object()`,
you might get an error message that looks something like this:

```
Assertion 'self.depth == 0' failed in file "/some/path/to/jou/stdlib/json.jou", line 192.
```

When this happens, check your `end_array()` and `end_object()` calls.
It's very easy to get them wrong.

The JSON builder does not check whether you use the same key multiple times
in the same JSON object, as in `{"a":1,"a":2}`.
Please don't do that.
JSON is supposed to work consistently with many different programming languages,
and almost all JSON parsers load JSON objects into a data structure
that does not allow multiple values for the same key, e.g. Python's `dict`.
