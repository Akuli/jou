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
    if toml_array != NULL:
        for p = toml_array.array.ptr; p < toml_array.array.end(); p++:
            printf("%d\n", toml_to_int(toml_get(p, "x"), -1))

    # Free memory used by the TOML object
    toml.free()

    return 0
```

