# Inline functions

Jou functions and methods can be decorated with the `@inline` decorator.
For example:

```python
import "stdlib/io.jou"

@inline
def increment(x: int) -> int:
    return x + 1

def main() -> int:
    printf("%d\n", increment(3))  # Output: 4
    return 0
```

This does the same thing as:

```python
import "stdlib/io.jou"

def main() -> int:
    printf("%d\n", 3 + 1)  # Output: 4
    return 0
```

In other words, when the program runs, there will be no function call.
An `@inline` function is basically a way to tell the compiler to copy/paste code.
However, `@inline` isn't literally copy/pasting.
For example, you don't need to worry about conflicting variable names:

```python
import "stdlib/io.jou"

@inline
def increment(x: int) -> int:
    x += 1
    return x

def main() -> int:
    x = 1
    y = 2
    z = increment(y)  # Does not do anything with x, does not change y
    printf("%d %d %d\n", x, y, z)  # Output: 1 2 3
    return 0
```

Unlike many other languages, Jou always inlines calls to `@inline` functions,
even if [optimizations](perf.md) are turned off with `-O0`.

Functions and methods marked with `@inline` cannot access global variables.
It would be possible to implement this in the compiler.
Please create an issue on GitHub if you need this.
To work around this, you could also create a non-`@inline` function
to access the global variable and call it.


## When to use `@inline`

The `@inline` decorator is useful for small and performance critical functions.
Usually inlining a long function is a bad idea.
That causes the compiler to copy/paste a lot of code,
which means that the compiled executable will be bigger.
Also, it probably won't be noticably faster with `@inline`.

For example, if you do game programming,
you might have a function that adds two 3D vectors, represented as `float[3]` arrays
(or perhaps with [a class](classes.md)):

```python
import "stdlib/io.jou"

@inline
def vec3_add(a: float[3], b: float[3]) -> float[3]:
    return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]

def main() -> int:
    added = vec3_add([1f, 2f, 3f], [4f, 5f, 6f])
    printf("%.2f %.2f %.2f\n", added[0], added[1], added[2])  # Output: 5.00 7.00 9.00
    return 0
```

This function is probably used in many places,
so the game may run much faster if it can just do the calculation without a function call.

Of course, this also works if you use [a class](classes.md):

```python
import "stdlib/io.jou"

class Vec3:
    x: float
    y: float
    z: float

    def print(self: Vec3) -> None:
        printf("%.2f %.2f %.2f\n", self.x, self.y, self.z)

    @inline
    def add(self: Vec3, other: Vec3) -> Vec3:
        return Vec3{
            x = self.x + other.x,
            y = self.y + other.y,
            z = self.z + other.z,
        }

def main() -> int:
    Vec3{x=1, y=2, z=3}.add(Vec3{x=4, y=5, z=6}).print()  # Output: 5.00 7.00 9.00
    return 0
```

