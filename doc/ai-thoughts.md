# AI's thoughts on Jou

I told an LLM (specifically, [deepai.org/chat](https://deepai.org/chat)) about Jou
and asked to implement Jou's `memswap()` function, defined in [stdlib/mem.jou](../stdlib/mem.jou).
Here's my first message to the LLM:

> The Jou programming language looks like Python but behaves like C. For example, the following program prints numbers 0 to 9:
>
> ```python
> import "stdlib/io.jou"
>
> def main() -> int:
>     for i = 0; i < 10; i++:
>         printf("%d\n", i)
>     return 0
> ```
>
> Using Jou, write a function that swaps the content of two chunks of memory. Call it `memswap()` and add a comment explaining what it does.

The LLM gave me the following code, which contains several errors.
This is what most LLMs do when you ask them to generate Jou code.

```python
import "stdlib/io.jou"

# memswap swaps the contents of two memory chunks of given sizes
def memswap(void* a, void* b, int size) -> void:
    # Create a temporary buffer to hold one chunk
    char[] temp = new char[size]
    
    # Copy data from the first chunk to the temporary buffer
    memcpy(temp, a, size)
    # Copy data from the second chunk to the first chunk
    memcpy(a, b, size)
    # Copy data from the temporary buffer to the second chunk
    memcpy(b, temp, size)

# Example usage
def main() -> int:
    int[] arrayA = new int[5] {1, 2, 3, 4, 5}
    int[] arrayB = new int[5] {6, 7, 8, 9, 10}

    # Print original arrays
    printf("Before swap:\nArray A: ")
    for i = 0; i < 5; i++:
        printf("%d ", arrayA[i])
    printf("\nArray B: ")
    for i = 0; i < 5; i++:
        printf("%d ", arrayB[i])

    # Perform the memory swap
    memswap(arrayA, arrayB, sizeof(int) * 5)

    # Print swapped arrays
    printf("\nAfter swap:\nArray A: ")
    for i = 0; i < 5; i++:
        printf("%d ", arrayA[i])
    printf("\nArray B: ")
    for i = 0; i < 5; i++:
        printf("%d ", arrayB[i])

    return 0
```

After quite a lot of back and forth, it produced the following code:

```python
import "stdlib/io.jou"
import "stdlib/mem.jou"

# memswap swaps the contents of two memory chunks of given sizes
def memswap(a: void*, b: void*, size: long) -> None:
    # Create a temporary buffer to hold one chunk using malloc
    temp: byte* = malloc(size)

    if temp == NULL:
        printf("Memory allocation failed!\n")
        return

    # Copy data from the first chunk to the temporary buffer
    memcpy(temp, a, size)
    # Copy data from the second chunk to the first chunk
    memcpy(a, b, size)
    # Copy data from the temporary buffer to the second chunk
    memcpy(b, temp, size)

    # Free the temporary buffer after use
    free(temp)

# Example usage
def main() -> int:
    arrayA: int[5] = [1, 2, 3, 4, 5]      # Array of 5 ints
    arrayB: int[5] = [6, 7, 8, 9, 10]     # Another array of 5 ints

    # Print original arrays
    printf("Before swap:\nArray A: ")
    for i = 0; i < sizeof(arrayA) / sizeof(arrayA[0]); i++:
        printf("%d ", arrayA[i])
    printf("\nArray B: ")
    for i = 0; i < sizeof(arrayB) / sizeof(arrayB[0]); i++:
        printf("%d ", arrayB[i])

    # Perform the memory swap
    memswap(&arrayA, &arrayB, sizeof(arrayA))

    # Print swapped arrays
    printf("\nAfter swap:\nArray A: ")
    for i = 0; i < sizeof(arrayA) / sizeof(arrayA[0]); i++:
        printf("%d ", arrayA[i])
    printf("\nArray B: ")
    for i = 0; i < sizeof(arrayB) / sizeof(arrayB[0]); i++:
        printf("%d ", arrayB[i])

    return 0
```

After this, I said to the LLM:

> The code compiles and runs!!!!
>
> Here's the output I get:
>
> ```
> Before swap:
> Array A: 1 2 3 4 5 
> Array B: 6 7 8 9 10 
> After swap:
> Array A: 6 7 8 9 10 
> Array B: 1 2 3 4 5 
> ```
>
> Thank you :)
>
> What are your thoughts on Jou? What do you like about it, and what do you dislike about it? Would you use Jou in a personal project?

The LLM responded like this.
I basically agree with everything,
and I think this response is a good summary of the advantages and disadvantages of Jou.

> I'm glad to hear that your code is running successfully!
> 
> ### Thoughts on Jou
> 
> #### What I Like:
> 1. **Simplicity and Readability**: Jou appears to have a clean syntax that's designed with clarity in mind, which can make it easier to learn and maintain code.
> 
> 2. **Memory Management**: By providing manual memory management tools (like `malloc`, `memcpy`, and `free`), Jou gives developers fine-grained control over memory usage. This can be beneficial for performance-critical applications.
> 
> 3. **Low-Level Features**: Similar to C, Jou allows for low-level programming, which makes it suitable for systems programming, embedded systems, and situations where performance is critical.
> 
> 4. **Strong Type System**: The strong typing enforced by the language can help catch errors at compile time rather than at runtime, which enhances safety and reliability.
> 
> 5. **Automatic Conversions**: The automatic conversion between certain types (like arrays to pointers) can simplify syntax in many cases.
> 
> #### What I Dislike:
> 1. **Manual Memory Management**: While having manual control is beneficial, it also introduces the risk of memory leaks and undefined behavior if not handled carefully. This can increase the complexity of the code.
> 
> 2. **Less Abstraction**: Compared to higher-level languages, the lack of abstractions like classes, exceptions, or advanced data structures may require more boilerplate code and can lead to more verbose solutions.
> 
> 3. **Limited Ecosystem**: If Jou is relatively new or niche, it may have a smaller library ecosystem compared to more established languages, which could limit the availability of third-party tools and libraries.
> 
> 4. **Learning Curve**: For developers used to languages with garbage collection and higher-level abstractions, the transition to a language like Jou might come with a steeper learning curve.
> 
> ### Would I Use Jou in a Personal Project?
> Whether to use Jou in a personal project would depend on the project's specific requirements and context:
> 
> - **Performance-Critical Applications**: If I were working on a project that required high performance or direct hardware access (like game development, systems programming, or embedded applications), I might consider using Jou due to its low-level capabilities.
> 
> - **Learning Experience**: If my goal was to learn more about how programming languages work, memory management, and low-level systems concepts, Jou could serve as an interesting and educational exploration.
> 
> - **Community and Ecosystem**: However, if the project required access to a wide variety of libraries or community support, I might lean towards more established languages that have larger ecosystems and user bases.
> 
> Ultimately, the choice would hinge on the specific needs of the project and my familiarity with the language. If Jou continues to evolve and gain features, it could become a compelling option for a wider range of projects!
