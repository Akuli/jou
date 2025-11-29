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

The LLM gave me the following code, which contains several syntax errors.
This is what most LLMs do when you ask them to generate Jou code.
After quite a lot of back and forth, all errors were fixed and the code worked.

<details><summary>Initial code (doesn't compile)</summary>

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

</details>

<details><summary>Fixed code</summary>

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

</details>

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


## 2025 Update

About a year later, I did more similar experiments by making an LLM write Jou code.
I used Microsoft's copilot set to "Smart (GPT-5)" (the default).
I don't like other things that Microsoft also calls "copilot",
but at the time of writing this, [copilot.microsoft.com](https://copilot.microsoft.com/) is free to use and requires no account.

This time I copy/pasted compiler error messages to AI directly, as if I was new to Jou myself.
This turned out to be a great way to find bad compiler error messages.
See these pull requests:
- [#1081 Improve various error messages](https://github.com/Akuli/jou/pull/1081) (6 improved compiler errors)
- [#1090 Improve compiler error message for Python-style type conversions like `int(foo)`](https://github.com/Akuli/jou/pull/1090)
- [#1091 Improve compiler error message for types like `[10]int` or `*int`](https://github.com/Akuli/jou/pull/1091)
- [#1092 Rename stdlib/rand.jou to stdlib/random.jou](https://github.com/Akuli/jou/pull/1092)
- [#1093 Improve compiler error for trying to use char as a type](https://github.com/Akuli/jou/pull/1093)

For example, here's a working number guessing game that the AI ended up with:

```python
import "stdlib/io.jou"
import "stdlib/random.jou"
import "stdlib/time.jou"
import "stdlib/str.jou"

def main() -> int:
    srand(time(NULL) as uint32)
    secret: int = (rand() % 100) + 1

    printf("I'm thinking of a number between 1 and 100.\n")

    buf: byte[100]  # buffer for input

    while True:
        printf("Your guess: ")

        # read a line from stdin into buf
        fgets(&buf[0], 100, get_stdin())

        # convert to int
        guess: int = atoi(&buf[0])

        if guess < secret:
            printf("Too low!\n")
        elif guess > secret:
            printf("Too high!\n")
        else:
            printf("Correct! The number was %d.\n", secret)
            break

    return 0
```

After writing the above program, I asked the AI:

> Thanks! This works!!!
>
> What do you think of Jou? What are its advantages and disadvantages based on what you've seen so far?

Here's what it said:

> **Quick take:** Jou is a fascinating language — it blends C‑like low‑level control with a modern, strict type system and a small but clear standard library. Its **advantages** are simplicity, explicitness, and closeness to C, while its **disadvantages** are a still‑limited ecosystem and the need to write more boilerplate compared to higher‑level languages.
>
> ### ⚡ Advantages of Jou
>
> - **C‑like familiarity:** Jou feels very close to C in syntax and semantics. If you know C, you can pick up Jou quickly.
> - **Strict typing:** The compiler enforces explicit conversions (`as uint32`, pointer syntax like `byte*`), which reduces hidden bugs and makes behavior predictable.
> - **Minimal standard library:** Modules like `stdlib/random.jou`, `stdlib/time.jou`, and `stdlib/io.jou` provide just enough functionality without overwhelming complexity.
> - **Transparency:** You always see what’s happening — no “magic” string types or implicit conversions. This makes Jou good for learning how systems programming works.
> - **Deterministic behavior:** Because Jou avoids implicit features, programs behave consistently across runs and platforms.
>
> ### ⚠️ Disadvantages of Jou
>
> - **Small ecosystem:** Unlike Python or JavaScript, Jou doesn’t yet have a large library ecosystem. You often need to implement things yourself.
> - **Verbose boilerplate:** Reading input requires buffers and `fgets`‑style calls, rather than a simple `input()` function. This makes quick scripting less convenient.
> - **Steep learning curve for beginners:** The strictness (pointer types, explicit casts, manual seeding) can be intimidating if you’re new to programming.
> - **Limited tooling:** Debuggers, IDE support, and package managers are still minimal compared to mature languages.
> - **Not widely adopted:** Because Jou is niche, you won’t find as much community support, tutorials, or Stack Overflow answers compared to mainstream languages.
>
> ### 🧩 Where Jou shines
>
> - **Educational use:** Jou is excellent for teaching how C‑style languages work, but with stricter rules that force good habits.
> - **Systems programming practice:** It’s close enough to C to let you experiment with low‑level concepts safely.
> - **Small projects or demos:** Perfect for exercises like your guess‑the‑number game, where you want to see how things work under the hood.
>
> ### 🚧 Where Jou struggles
>
> - **Rapid prototyping:** If you want to build something quickly, Python or JavaScript will get you there faster.
> - **Large applications:** Without a big ecosystem, scaling Jou projects can be tough.
> - **Cross‑platform libraries:** Jou doesn’t yet have the breadth of bindings to external systems that C or Rust enjoy.
>
> **In short:** Jou is a lean, strict, C‑like language that’s great for learning and experimenting, but not yet practical for large‑scale or rapid development. It forces you to understand pointers, types, and conversions — which is both its biggest strength and its biggest hurdle.
>
> Would you like me to compare Jou directly with **Rust** or **Go**, since they’re also modern systems languages with stricter rules but larger ecosystems? That might highlight where Jou fits in the bigger programming landscape.

Of course, I asked the AI to compare Jou to Rust when it suggested that:

> Direct answer: Jou is a minimalist, C‑like language focused on explicitness and simplicity, while Rust is a mature systems language designed for safety, concurrency, and performance. Rust’s ecosystem and tooling are far more advanced, but Jou’s strictness makes it a good teaching tool for understanding low‑level programming.
>
> ### ⚡ Jou vs. Rust: Key Comparison
>
> | Aspect | Jou | Rust |
> |---|---|---|
> | **Design goal** | Simplicity, explicit typing, C‑like syntax | Safety, speed, concurrency |
> | **Type system** | Strict, manual casts (as uint32), pointers (byte*) | Ownership & borrowing system prevents data races at compile time |
> | **Memory safety** | Manual, similar to C (risk of misuse) | Compiler‑enforced safety without garbage collection |
> | **Standard library** | Minimal (io.jou, random.jou, time.jou) | Rich ecosystem with crates for networking, async, crypto, etc. |
> | **Ease of use** | Lightweight, but verbose (manual buffers, fgets) | Steeper learning curve, but modern syntax and abstractions |
> | **Community & adoption** | Niche, experimental | Large, thriving, “most loved” language in surveys |
> | **Best use cases** | Education, small demos, learning pointers/types | Systems programming, web backends, embedded, performance‑critical apps |
>
> ### 🔎 Advantages of Jou
>
> - **Minimalism:** Forces you to understand pointers, buffers, and explicit type conversions.
> - **Transparency:** No hidden behavior — you see exactly what the compiler enforces.
> - **Educational value:** Great for learning how C‑style languages work without legacy baggage.
>
> ### 🔎 Advantages of Rust
>
> - **Memory safety:** Ownership/borrowing prevents common bugs like dangling pointers or data races.
> - **Concurrency:** Built‑in support for safe multithreading.
> - **Ecosystem:** Thousands of crates, strong tooling, and wide adoption.
> - **Performance:** Comparable to C/C++ but safer.
>
> ### ⚠️ Disadvantages
>
> - **Jou:** Small ecosystem, verbose for simple tasks, limited tooling.
> - **Rust:** Steep learning curve, sometimes complex syntax, slower compile times.
>
> ### 🧩 Where they fit
>
> - **Jou:** Best for learning low‑level programming concepts, experimenting with strict typing, or writing small demos like your guess‑the‑number game.
> - **Rust:** Best for production systems programming where safety and performance matter — operating systems, embedded devices, or high‑performance servers.
>
> ------------
>
> **Summary:** Jou is like a teaching scalpel — sharp, minimal, and unforgiving. Rust is a full surgical toolkit — powerful, safe, and widely adopted. Jou helps you understand the low‑level mechanics; Rust helps you build real‑world systems safely.
