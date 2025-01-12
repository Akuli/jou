# Enums

TL;DR:

```python
import "stdlib/io.jou"

enum Foo:
    Bar
    Baz

def main() -> int:
    thing = Foo.Bar

    if thing == Foo.Bar:
        printf("It's bar\n")  # Output: It's bar
    elif thing == Foo.Baz:
        printf("It's baz\n")
    else:
        assert False  # never happens

    printf("%d\n", Foo.Bar)  # Output: 0
    printf("%d\n", Foo.Baz)  # Output: 1

    return 0
```


## Introduction

Let's say you are working on a calculator program,
and you have something like this:

```python
import "stdlib/io.jou"

def calculate(a: double, b: double, op: byte) -> double:
    if op == '+':
        return a + b
    elif op == '-':
        return a - b
    elif op == '*':
        return a * b
    else:
        assert False  # unsupported operation

def main() -> int:
    printf("%f\n", calculate(1, 2, '+'))  # Output: 3.000000
    return 0
```

Here `assert False` aborts the execution of the program
with an error message that contains the file name and line number.
This is useful when some part of the code should never run.
Here, if you leave it out, you will get a compiler warning,
because then the `calculate()` function doesn't return a value in all cases.

Now, suppose you need to add a division feature to the calculator.
The most common ways to do division are:
- "Normal" division: `7 / 3 = 2.33333...`
- Floor division: `7 / 3 = 2` (result rounded down)

It is tempting to use the `'/'` byte for normal division, but which byte to use for floor division?
Maybe `'f'` for floor?
If you ask any experienced programmer, they will probably tell you to use enums.
Here's how it works in Jou:

```python
import "stdlib/io.jou"
import "stdlib/math.jou"

enum Operation:
    Add
    Subtract
    Multiply
    Divide          # 7 / 3 produces 2.3333...
    FloorDivide     # 7 / 3 produces 2

def calculate(a: double, b: double, op: Operation) -> double:
    if op == Operation.Add:
        return a + b
    if op == Operation.Subtract:
        return a - b
    if op == Operation.Multiply:
        return a * b
    if op == Operation.Divide:
        return a / b
    if op == Operation.FloorDivide:
        return floor(a / b)
    else:
        assert False  # not possible

def main() -> int:
    printf("%f\n", calculate(7, 3, Operation.Divide))        # Output: 2.333333
    printf("%f\n", calculate(7, 3, Operation.FloorDivide))   # Output: 2.000000
    return 0
```

Here `enum Operation` defines a new enum, which has 5 possible values.
You can then use `if` statements to check which value an instance of `Operation` is.


## Integer conversions

When the program runs, enums are actually just `int`s.
The first enum member is `0`, the second is `1`, and so on.
You can use `as` to convert between enums and `int`s:

```python
import "stdlib/io.jou"

enum Operation:
    Add
    Subtract
    Multiply

def main() -> int:
    printf("%d\n", Operation.Add as int)       # Output: 0
    printf("%d\n", Operation.Subtract as int)  # Output: 1
    printf("%d\n", Operation.Multiply as int)  # Output: 2
    return 0
```

This is sometimes used to assign a string to each enum member:

```python
import "stdlib/io.jou"

enum Operation:
    Add
    Subtract
    Multiply

def main() -> int:
    descriptions = ["Add numbers", "Subtract numbers", "Multiply numbers"]
    printf("%s\n", descriptions[Operation.Subtract as int])  # Output: Subtract numbers
    return 0
```

You can also convert integers to enums,
but note that the result might not correspond with any member of the enum:

```python
import "stdlib/io.jou"

enum Operation:
    Add
    Subtract
    Multiply

def main() -> int:
    wat = 7 as Operation

    if wat == Operation.Add:
        printf("Add\n")
    elif wat == Operation.Subtract:
        printf("Subtract\n")
    elif wat == Operation.Multiply:
        printf("Multiply\n")
    else:
        # Output: something else 7
        printf("something else %d\n", wat as int)

    return 0
```


## Debugging

Unfortunately, it is not possible to print the name of an enum member at runtime,
and you sometimes need to convert the enum to `int` and print the result (see above).
Please create an issue on GitHub to discuss this if it annoys you.
