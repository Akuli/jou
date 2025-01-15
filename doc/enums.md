# Enums

TL;DR:

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
        case Thingy.Bar | Thingy.Baz:
            printf("It's bar or baz\n")  # Output: It's bar or baz

    match thing:
        case Thingy.Foo:
            printf("It's foo\n")
        case _:
            printf("It's not foo\n")  # Output: It's not foo

    printf("%d\n", Thingy.Foo as int)  # Output: 0
    printf("%d\n", Thingy.Bar as int)  # Output: 1
    printf("%d\n", Thingy.Baz as int)  # Output: 2

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
    match op:
        case Operation.Add:
            return a + b
        case Operation.Subtract:
            return a - b
        case Operation.Multiply:
            return a * b
        case Operation.Divide:
            return a / b
        case Operation.FloorDivide:
            return floor(a / b)

def main() -> int:
    printf("%f\n", calculate(7, 3, Operation.Divide))        # Output: 2.333333
    printf("%f\n", calculate(7, 3, Operation.FloorDivide))   # Output: 2.000000
    return 0
```

Here `enum Operation` defines a new enum, which has 5 members.
You will get a compiler error if you don't handle all 5 members in the `match` statement.
This is useful especially when adding a new feature to a large existing program.
Instead of a `match` statement, you can also use `if` and `elif` with enums,
but then the compiler won't complain if you don't handle all enum members.

Sometimes you don't want to list all enum members in a `match` statement.
You can use `case _:` to catch all remaining enum members:

```python
import "stdlib/io.jou"

enum Operation:
    Add
    Subtract
    Multiply
    Divide
    FloorDivide

def main() -> int:
    op = Operation.Divide

    match op:
        case Operation.Add:
            printf("It's adding\n")
        case Operation.Subtract:
            printf("It's subtracting\n")
        case _:
            printf("Not adding or subtracting\n")  # Output: Not adding or subtracting

    return 0
```

You can also combine multiple cases with `|`.
Read the `|` operator as "or" when it's used in match statements.

```python
import "stdlib/io.jou"

enum Operation:
    Add
    Subtract
    Multiply
    Divide
    FloorDivide

def main() -> int:
    op = Operation.FloorDivide

    match op:
        case Operation.Divide | Operation.FloorDivide:
            printf("It's dividing\n")  # Output: It's dividing
        case _:
            pass

    return 0
```

Here `case _: pass` is needed to ignore the enum members that were not mentioned,
because without it, you will get a compiler error saying that you didn't handle all possible values.


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
but note that the result might not correspond with any member of the enum.
In a `match` statements, only `case _:` matches these values.

```python
import "stdlib/io.jou"

enum Operation:
    Add
    Subtract
    Multiply

def main() -> int:
    wat = 42 as Operation

    match wat:
        case Operation.Add:
            printf("Add\n")
        case Operation.Subtract:
            printf("Subtract\n")
        case Operation.Multiply:
            printf("Multiply\n")
        case _:
            # Output: something else 42
            printf("something else %d\n", wat as int)

    return 0
```


## Debugging

Unfortunately, it is not possible to print the name of an enum member at runtime,
and you sometimes need to convert the enum to `int` and print the result (see above).
Please create an issue on GitHub to discuss this if it annoys you.
