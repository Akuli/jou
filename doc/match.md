# Match Statements

Jou has a `match` statement that is meant to be used instead of long `if`/`elif` chains.

For example, consider the following code:

```python
import "stdlib/io.jou"

def main() -> int:
    number = 4
    if number == 0:
        printf("Zero\n")
    elif number == 1:
        printf("One\n")
    elif number == 2:
        printf("Two\n")
    elif number == 3:
        printf("Three\n")
    elif number == 4:
        printf("Four\n")  # Output: Four
    elif number == 5:
        printf("Five\n")
    else:
        printf("Many\n")
    return 0
```

This can be written with a `match` statement:

```python
import "stdlib/io.jou"

def main() -> int:
    number = 4
    match number:
        case 0:
            printf("Zero\n")
        case 1:
            printf("One\n")
        case 2:
            printf("Two\n")
        case 3:
            printf("Three\n")
        case 4:
            printf("Four\n")  # Output: Four
        case 5:
            printf("Five\n")
        case _:
            printf("Many\n")
    return 0
```

The `case _` is special syntax that means "anything else".
It may be omitted if you don't want to do anything for other values.

Match statements can only be used with [integer types](types.md#integers) (`int` in this example) and [enums](enums.md).
Jou [doesn't have very many different kinds of types](types.md), so this covers most use cases.


## Using `match ... with strcmp` to match strings

In Jou, [strings are pointers](tutorial.md#more-about-strings).
They cannot be compared with `==`,
because two different strings could point to different memory locations that contain the same text.
Instead, [stdlib/str.jou](../stdlib/str.jou) contains the function `strcmp(string1, string2)`,
which returns 0 if the strings are equal, and a nonzero value if they are not equal.

For example:

```python
import "stdlib/io.jou"
import "stdlib/str.jou"

def main() -> int:
    s1 = "Hello"
    s2_full = "xHello"
    s2 = &s2_full[1]

    printf("s1=%s s2=%s\n", s1, s2)  # Output: s1=Hello s2=Hello

    if s1 == s2:
        printf("Same pointer\n")
    else:
        printf("Not the same pointer\n")  # Output: Not the same pointer

    if strcmp(s1, s2) == 0:
        printf("Contains the same text\n")  # Output: Contains the same text
    else:
        printf("Contains different text\n")

    return 0
```

To support matching strings, you can specify `with strcmp` in a match statement.
This causes the match statement to call `strcmp(string1, string2)` when comparing instead of `==`.
For example:

```python
import "stdlib/io.jou"
import "stdlib/str.jou"

def main() -> int:
    s = "Bar"

    match s with strcmp:
        case "Foo":     # this calls strcmp(s, "Foo")
            printf("It's foo\n")
        case "Bar":
            printf("It's bar\n")  # Output: It's bar
        case "Baz":
            printf("It's baz\n")

    return 0
```

Instead of `strcmp`, you can specify the name of any function
that takes two arguments and returns an integer.
Return value zero means "equal", and anything else means "not equal".
This way `match ... with` can be extended to compare **any** objects in any way you want,
if you write an appropriate comparing function.


## Combining multiple cases with `|`

You can use `|` to match multiple possible values. For example:

```python
import "stdlib/io.jou"

def main() -> int:
    character = 'x'

    match character:
        case '0' | '1' | '2' | '3' | '4' | '5' | '6' | '7' | '8' | '9':
            printf("It's a number!\n")
        case _:
            printf("Not a number\n")  # Output: Not a number

    return 0
```

Sometimes it's more convenient to write the combined cases on separate lines, like this:

```python
import "stdlib/io.jou"

def main() -> int:
    character = 'x'

    match character:
        case (
            '0'
            | '1'
            | '2'
            | '3'
            | '4'
            | '5'
            | '6'
            | '7'
            | '8'
            | '9'
        ):
            printf("It's a number!\n")
        case _:
            printf("Not a number\n")  # Output: Not a number

    return 0
```

This also applies to `match ... with`.

Note that the `|` operator is also used for [bitwise OR](bitwise.md#bitwise-or).


## Special-casing for enums

Matching [enums](enums.md) with `match` statements works mostly like you would expect,
but the compiler knows which enum members each `case` handles and acts accordingly.
For example, you will get an error if you don't match all enum members:

```python
import "stdlib/io.jou"

enum Thingy:
    Foo
    Bar
    Baz

def main() -> int:
    thing = Thingy.Bar

    match thing:  # Error: enum member Thingy.Baz not handled in match statement
        case Thingy.Foo:
            printf("It's foo\n")
        case Thingy.Bar:
            printf("It's bar\n")

    return 0
```

To fix the error, you can add a `case Thingy.Baz` with [a `pass` statement](keywords.md#pass):

```python
import "stdlib/io.jou"

enum Thingy:
    Foo
    Bar
    Baz

def main() -> int:
    thing = Thingy.Bar

    match thing:
        case Thingy.Foo:
            printf("It's foo\n")
        case Thingy.Bar:
            printf("It's bar\n")  # Output: It's bar
        case Thingy.Baz:
            pass

    return 0
```

You can also add `case _: pass` if you want ignore all other possible enum members.


## The weird corner case where a `match` statement produces UB

Consider the following code:

```python
import "stdlib/io.jou"

enum Thingy:
    Foo
    Bar
    Baz

def thingy_to_string(t: Thingy) -> byte*:
    match t:
        case Thingy.Foo:
            return "It's a foo"
        case Thingy.Bar:
            return "Barbar"
        case Thingy.Baz:
            return "Bazzz"
    # We never get here, because we return in all cases

def main() -> int:
    printf("%s\n", thingy_to_string(Thingy.Baz))  # Output: Bazzz
    return 0
```

This code looks pretty and works well.
The `thingy_to_string()` function always returns a value,
because the `match` statement handles all possible cases... or does it?
How about `thingy_to_string(7 as Thingy)`?
After all, [an enum can hold any `int` value](enums.md#integer-conversions).

Jou's solution is that you simply shouldn't do `thingy_to_string(7 as Thingy)`.
Specifically, a `match` statement over an enum without `case _` invokes [Undefined Behavior](ub.md)
if the given value is not a valid member of the enum.

If you need to support invalid enum values in the `thingy_to_string()` function,
you can simply add a `case _` to catch them:

```python
import "stdlib/io.jou"

enum Thingy:
    Foo
    Bar
    Baz

def thingy_to_string(t: Thingy) -> byte*:
    match t:
        case Thingy.Foo:
            return "It's a foo"
        case Thingy.Bar:
            return "Barbar"
        case Thingy.Baz:
            return "Bazzz"
        case _:
            return "Invalid"

def main() -> int:
    printf("%s\n", thingy_to_string(7 as Thingy))  # Output: Invalid
    return 0
```
