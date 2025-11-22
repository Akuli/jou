# Classes

TL;DR:

```python
import "stdlib/io.jou"

class Person:
    name: byte*

    # self is a pointer (Person*)
    def greet(self) -> None:
        printf("Hello %s\n", self.name)

def main() -> int:
    instance = Person{name="world"}
    instance.greet()   # Output: Hello world
    return 0
```


## Fields

An instance of a Jou class is a chunk of memory that is large enough to store multiple values.
For example, the following class has two integers:

```python
class Point:
    x: int
    y: int
```

Now every instance of `Point` will be at least 64 bits in size:
32 bits for `x`, and 32 bits for `y`.
In reality, instances may be bigger than expected due to
[padding](https://stackoverflow.com/questions/4306186/structure-padding-and-packing),
but this can be almost always ignored.

To create an instance of `Point`,
we simply need to take enough memory to hold the two `int`s,
and we need to tell the compiler to treat that memory as a `Point` instance.
In other words, we create a variable whose type is `Point`:

```python
p: Point
```

Because the memory is [uninitialized](tutorial.md#undefined-behavior-ub),
we still need to assign values to the fields, which we can access as `p.x` and `p.y`.
Like this:

```python
import "stdlib/io.jou"

class Point:
    x: int
    y: int

def main() -> int:
    p: Point
    p.x = 12
    p.y = 34
    printf("%d, %d\n", p.x, p.y)  # Output: 12, 34

    p.y++
    printf("%d, %d\n", p.x, p.y)  # Output: 12, 35
    return 0
```

Alternatively, we could create the instance in one line with `p = Point{x = 12, y = 34}`.
This syntax is explained in detail [below](#instantiating-syntax).


## Pointers

Instances of classes are often passed around as pointers.
To understand why, let's try to make a function
that increments the `y` coordinate of a `Point`:

```python
import "stdlib/io.jou"

class Point:
    x: int
    y: int

def increment_y(instance: Point) -> None:
    instance.y++  # Doesn't work as expected

def main() -> int:
    p = Point{x = 12, y = 34}
    increment_y(p)
    printf("%d\n", p.y)  # Output: 34
    return 0
```

The problem is that when we do `increment_y(p)`,
we simply pass the 64 (or more) bytes of the instance `p` to the `increment_y()` function.
This is very similar to creating two variables `x` and `y` in the `main()` function:

```python
import "stdlib/io.jou"

def increment_y(x: int, y: int) -> None:
    y++  # Doesn't work as expected

def main() -> int:
    x = 12
    y = 34
    increment_y(x, y)
    printf("%d, %d\n", x, y)  # Output: 12, 34
    return 0
```

In either case, the `increment_y()` function gets a copy of the coordinates,
so `instance.y++` or `y++` only increments the `y` coordinate of the copy.

For this reason, instances of classes are often passed around as [pointers](tutorial.md#pointers).
This way the `increment_y()` function knows where the original instance is in the computer's memory,
so that it can place the new value there instead of its own copy of the instance.
Like this:

```python
import "stdlib/io.jou"

class Point:
    x: int
    y: int

def increment_y(ptr: Point*) -> None:
    ptr.y++

def main() -> int:
    p = Point{x = 12, y = 34}
    increment_y(&p)
    printf("%d\n", p.y)  # Output: 35
    return 0
```

Here `ptr.y` does the same thing as `(*ptr).y`:
it accesses the `y` member of the instance located wherever the pointer `ptr` is pointing.
For convenience, the `.` operator can also be applied to a pointer to an instance of a class.


## Methods

The above `increment_y()` function does something with a point,
so instead of a function, we can also write it as a method in the `Point` class:

```python
import "stdlib/io.jou"

class Point:
    x: int
    y: int

    def increment_y(self) -> None:
        self.y++

def main() -> int:
    p = Point{x=12, y=34}
    p.increment_y()
    printf("%d\n", p.y)  # Output: 35
    return 0
```

By default, methods take the instance as a pointer.
In the above example, the type of `self` is `Point*`,
which means that `self` is a pointer to an instance of `Point`.

To call a method on a pointer (such as `self`),
you can simply use `.`, just like with accessing fields:

```python
import "stdlib/io.jou"

class Point:
    x: int
    y: int

    def increment_x(self) -> None:
        self.x++

    def increment_y(self) -> None:
        self.y++

    def increment_both(self) -> None:
        self.increment_x()
        self.increment_y()

def main() -> int:
    p = Point{x=12, y=34}
    p.increment_both()
    printf("%d %d\n", p.x, p.y)  # Output: 13 35
    return 0
```

Here `self.increment_x()` is a shorthand for `(*self).increment_x()`:
it accesses the instance through the `self` pointer and calls its `increment_x()` method.

If, for some reason, you want to pass the instance by value instead of a pointer,
so that the method gets a copy of it,
you can specify the type of `self` like this:

```python
class Point:
    def do_something(self: Point) -> None:
        ...
```

This means that the type of `self` is `Point`, not `Point*` (the default),
so `self` is not a pointer.


## Instantiating Syntax

As we have seen, "instantiating" simply means taking a chunk of memory of the correct size,
but it's often done with the `ClassName{field=value}` syntax.
Let's look at this syntax in more detail.

The curly braces are used to distinguish instantiating syntax from function calls.
This makes the Jou compiler simpler, but if you don't like this syntax,
feel free to [create an issue](https://github.com/Akuli/jou/issues/new) to discuss it.

If you omit some class fields, they will be initialized to zero.
Specifically, the memory used for the fields will be all zero bytes.
This means that boolean fields are set to `False`,
numbers are set to zero,
pointer fields become `NULL`,
and fixed size strings appear as empty:

```python
import "stdlib/io.jou"


class Person:
    name: byte*
    country: byte[50]

    def introduce(self) -> None:
        if self.name == NULL:
            printf("I'm an anonymous person from '%s'\n", self.country)
        else:
            printf("I'm %s from '%s'\n", self.name, self.country)


def main() -> int:
    akuli = Person{name="Akuli", country="Finland"}
    akuli.introduce()  # Output: I'm Akuli from 'Finland'

    akuli = Person{name="Akuli"}
    akuli.introduce()  # Output: I'm Akuli from ''

    akuli = Person{}
    akuli.introduce()  # Output: I'm an anonymous person from ''

    return 0
```

You can achieve the same thing by setting the memory used by the instance to zero bytes.
This is often done with the `memset()` function from [stdlib/mem.jou](../stdlib/mem.jou).
It takes in three parameters, so that `memset(ptr, 0, n)` sets `n` bytes starting at pointer `ptr` to zero.

To calculate the correct `n`, you can use `sizeof(instance)`, where `instance` is any instance of the class.
It doesn't matter which instance you use, because all instances of the class are of the same size.
In general, the value of `sizeof(x)` only depends on the type of `x`,
and it doesn't even evaluate `x` when the program runs.

For example, the following program creates an array of three uninitialized instances of `Person`,
and then zero-initializes all of them using `memset()`.
Array elements are simply next to each other in memory,
so it's enough to do one `memset()` that is big enough to set all of them to zero.
Like this:

```python
import "stdlib/io.jou"
import "stdlib/mem.jou"


class Person:
    name: byte*
    country: byte[50]

    def introduce(self) -> None:
        if self.name == NULL:
            printf("I'm an anonymous person from '%s'\n", self.country)
        else:
            printf("I'm %s from '%s'\n", self.name, self.country)


def main() -> int:
    people: Person[3]
    memset(&people, 0, sizeof(people[0]) * 3)

    # Output: I'm an anonymous person from ''
    # Output: I'm an anonymous person from ''
    # Output: I'm an anonymous person from ''
    for i = 0; i < 3; i++:
        people[i].introduce()

    return 0
```

Instead of `sizeof(people[0]) * 3`, you could just as well use `sizeof(people)`.
The size of an array of 3 elements is simply 3 times the size of one element.
You could also use `people = [Person{}, Person{}, Person{}]` to create and zero-initialize the array,
but this becomes annoying if the array contains many instances.
