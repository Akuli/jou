# Classes

TL;DR:

```python
import "stdlib/io.jou"

class Person:
    name: byte*

    def greet(self) -> None:
        printf("Hello %s\n", self->name)   # self is a pointer (Person*)

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

You can use e.g. `Point{x=12, y=34}` to instantiate the class,
and the usual `.` syntax to access its fields:

```python
import "stdlib/io.jou"

class Point:
    x: int
    y: int

def main() -> int:
    p = Point{x=12, y=34}
    printf("%d, %d\n", p.x, p.y)  # Output: 12, 34
    p.y++
    printf("%d, %d\n", p.x, p.y)  # Output: 12, 35
    return 0
```

This does not allocate any heap memory (TODO: document heap allocations).
In fact, it's basically same as creating two variables `x` and `y` in the `main()` function:

```python
import "stdlib/io.jou"

def main() -> int:
    x = 12
    y = 34
    printf("%d, %d\n", x, y)  # Output: 12, 34
    return 0
```

This means that if you pass an instance of a class to a function, you get a copy,
as if you had just passed the two integers:

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
    ptr->y++

def main() -> int:
    p = Point{x = 12, y = 34}
    increment_y(&p)
    printf("%d\n", p.y)  # Output: 35
    return 0
```

Here `ptr->y` does the same thing as `(*ptr).y`:
it accesses the `y` member of the instance located wherever the pointer `ptr` is pointing.


## Methods

The above `increment_y()` function does something with a point,
so instead of a function, it would be better to write it as a method in the `Point` class:

```python
import "stdlib/io.jou"

class Point:
    x: int
    y: int

    def increment_y(self) -> None:
        self->y++

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
use `->`, just like with accessing fields:

```python
import "stdlib/io.jou"

class Point:
    x: int
    y: int

    def increment_x(self) -> None:
        self->x++

    def increment_y(self) -> None:
        self->y++

    def increment_both(self) -> None:
        self->increment_x()
        self->increment_y()

def main() -> int:
    p = Point{x=12, y=34}
    p.increment_both()
    printf("%d %d\n", p.x, p.y)  # Output: 13 35
    return 0
```

If, for some reason, you want to pass the instance by value instead of a pointer,
you can specify the type of `self` like this:

```python
import "stdlib/io.jou"

class Point:
    x: int
    y: int

    def increment_y(self: Point) -> None:  # pass self by value
        self.y++
        printf("incremented to %d\n", self.y)

def main() -> int:
    p = Point{x=12, y=34}
    p.increment_y()             # Output: incremented to 35
    printf("still %d\n", p.y)   # Output: still 34
    return 0
```


## Instantiating

As we have seen, the instantiating syntax is `ClassName{field=value}`.
The curly braces are used to distinguish instantiating from function calls.

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
        if self->name == NULL:
            printf("I'm an anonymous person from '%s'\n", self->country)
        else:
            printf("I'm %s from '%s'\n", self->name, self->country)


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
To calculate the correct `n`, you can use the `sizeof` operator (TODO: document sizeof):

```python
import "stdlib/io.jou"
import "stdlib/mem.jou"


class Person:
    name: byte*
    country: byte[50]

    def introduce(self) -> None:
        if self->name == NULL:
            printf("I'm an anonymous person from '%s'\n", self->country)
        else:
            printf("I'm %s from '%s'\n", self->name, self->country)


def main() -> int:
    akuli = Person{name="Akuli", country="Finland"}
    memset(&akuli, 0, sizeof(akuli))
    akuli.introduce()  # Output: I'm an anonymous person from ''
    return 0
```

This works the same way if you have multiple instances next to each other in memory,
such as in an array, and you want to zero-initialize all of them:

```python
import "stdlib/io.jou"
import "stdlib/mem.jou"

class Person:
    name: byte*
    country: byte[50]

    def introduce(self) -> None:
        if self->name == NULL:
            printf("I'm an anonymous person from '%s'\n", self->country)
        else:
            printf("I'm %s from '%s'\n", self->name, self->country)

def main() -> int:
    contributors = [
        Person{name="Akuli", country="Finland"},
        Person{name="littlewhitecloud", country="China"},
        Person{name="Moosems", country="USA"},
    ]

    # Output: I'm Akuli from 'Finland'
    # Output: I'm littlewhitecloud from 'China'
    # Output: I'm Moosems from 'USA'
    for i = 0; i < 3; i++:
        contributors[i].introduce()

    memset(&contributors, 0, sizeof(contributors))

    # Output: I'm an anonymous person from ''
    # Output: I'm an anonymous person from ''
    # Output: I'm an anonymous person from ''
    for i = 0; i < 3; i++:
        contributors[i].introduce()

    return 0
```

Here `sizeof(contributors)` is the size of the entire array in bytes,
which is 3 times the size of a `Person`.
