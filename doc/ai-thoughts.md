# AI's thoughts on Jou

I sometimes write Jou code with an LLM.
Because Jou is not a very popular programming language,
the Jou code usually doesn't work on the first try,
so I ask the LLM to fix the code until it compiles and runs correctly.

After that, we get to the interesting part: I ask what the LLM thinks of the Jou programming language.
So far this has always resulted in the LLM pointing out things that are
**obvious to me, but not to someone new to Jou**.
This is great, because I'm not good at documenting such things :)


## 2024: writing a `memswap()` function

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


## 2025: number guessing game

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
- [#1099 Improve compiler error messages for input(), stdin, stdout, stderr](https://github.com/Akuli/jou/pull/1099)

<details><summary>A working number guessing game written by AI</summary>

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

</details>

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
> ------------
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
> ------------
>
> ### ⚠️ Disadvantages
>
> - **Jou:** Small ecosystem, verbose for simple tasks, limited tooling.
> - **Rust:** Steep learning curve, sometimes complex syntax, slower compile times.
>
> ------------
>
> ### 🧩 Where they fit
>
> - **Jou:** Best for learning low‑level programming concepts, experimenting with strict typing, or writing small demos like your guess‑the‑number game.
> - **Rust:** Best for production systems programming where safety and performance matter — operating systems, embedded devices, or high‑performance servers.
>
> ------------
>
> **Summary:** Jou is like a teaching scalpel — sharp, minimal, and unforgiving. Rust is a full surgical toolkit — powerful, safe, and widely adopted. Jou helps you understand the low‑level mechanics; Rust helps you build real‑world systems safely.


## 2026: JSON builder

In February 2026, I asked an LLM to write a JSON builder similar to [tsoding's `jim` library](https://github.com/tsoding/jim/).
I used [copilot.microsoft.com](https://copilot.microsoft.com/) for the same reasons as in 2025.

I first asked the AI to write me a program that demonstrates how to use lists.
It was very far from correct, so I ended up giving [the documentation about lists](lists.md) as an attachment.
I later attached more documentation files.
Once that was working, I then asked the AI to implement the JSON builder:

> Your example based on the docs works. Thanks!
>
> I also attached Jou's documentation about classes. Please design and implement a JSON builder class in "immediate mode" style: if `jb` is a JSON builder, then `jb.array()` followed by `jb.integer(1)` and `jb.end()` should append `[1]` to `jb`'s internal output list. (This is immediate mode in the sense that you build the JSON string directly, not a data structure.) Feel free to change the API I suggested here as you see fit, and add a `main()` function to test the builder.

This is not exactly an easy thing to implement, but after some back and forth, the AI produced a working program:

<details><summary>A working JSON builder written by AI</summary>

```python
import "stdlib/io.jou"
import "stdlib/list.jou"
import "stdlib/mem.jou"
import "stdlib/assert.jou"
import "stdlib/str.jou"

enum Ctx:
    Array
    ObjectExpectKey
    ObjectExpectValue

const MAX_DEPTH: int = 32

class JsonBuilder:
    out: List[byte]

    ctx_stack: Ctx[MAX_DEPTH]
    comma_stack: int[MAX_DEPTH]   # 0 = no comma yet, 1 = comma needed
    depth: int

    def init(self) -> None:
        self.out = List[byte]{}
        self.depth = 0

    def free(self) -> None:
        free(self.out.ptr)

    # -------------------------
    #   OUTPUT HELPERS
    # -------------------------

    def putc(self, c: byte) -> None:
        self.out.append(c)

    def puts(self, s: byte*) -> None:
        for p = s; *p != 0; p++:
            self.out.append(*p)

    # -------------------------
    #   COMMA HANDLING
    # -------------------------

    # Only for arrays and for *pairs* in objects
    def comma_before_pair_or_array_value(self) -> None:
        if self.depth == 0:
            return

        ctx = self.ctx_stack[self.depth - 1]

        if ctx == Ctx.Array:
            if self.comma_stack[self.depth - 1] == 1:
                self.putc(',')
            self.comma_stack[self.depth - 1] = 1

        elif ctx == Ctx.ObjectExpectKey:
            if self.comma_stack[self.depth - 1] == 1:
                self.putc(',')
            # Do NOT set comma yet — only after finishing the value

        # If expecting a value, do NOT insert commas here

    def mark_pair_done(self) -> None:
        if self.depth == 0:
            return
        if self.ctx_stack[self.depth - 1] == Ctx.ObjectExpectValue:
            self.comma_stack[self.depth - 1] = 1
            self.ctx_stack[self.depth - 1] = Ctx.ObjectExpectKey

    # -------------------------
    #   STACK MANAGEMENT
    # -------------------------

    def push(self, ctx: Ctx) -> None:
        assert(self.depth < MAX_DEPTH)
        self.ctx_stack[self.depth] = ctx
        self.comma_stack[self.depth] = 0
        self.depth++

    def pop(self) -> Ctx:
        assert(self.depth > 0)
        self.depth--
        return self.ctx_stack[self.depth]

    # -------------------------
    #   STRUCTURE METHODS
    # -------------------------

    def begin_array(self) -> None:
        self.comma_before_pair_or_array_value()
        self.putc('[')
        self.push(Ctx.Array)

    def begin_object(self) -> None:
        self.comma_before_pair_or_array_value()
        self.putc('{')
        self.push(Ctx.ObjectExpectKey)

    def key(self, name: byte*) -> None:
        assert(self.depth > 0)
        assert(self.ctx_stack[self.depth - 1] == Ctx.ObjectExpectKey)

        self.comma_before_pair_or_array_value()

        self.raw_string(name)
        self.putc(':')

        self.ctx_stack[self.depth - 1] = Ctx.ObjectExpectValue

    def end(self) -> None:
        ctx = self.pop()

        if ctx == Ctx.Array:
            self.putc(']')
        else:
            assert(ctx == Ctx.ObjectExpectKey)
            self.putc('}')

        self.mark_pair_done()

    # -------------------------
    #   VALUE METHODS
    # -------------------------

    def integer(self, x: int) -> None:
        self.comma_before_pair_or_array_value()
        buf: byte[50]
        sprintf(&buf[0], "%d", x)
        self.puts(&buf[0])
        self.mark_pair_done()

    def boolean(self, b: int) -> None:
        self.comma_before_pair_or_array_value()
        if b != 0:
            self.puts("true")
        else:
            self.puts("false")
        self.mark_pair_done()

    def null(self) -> None:
        self.comma_before_pair_or_array_value()
        self.puts("null")
        self.mark_pair_done()

    # Raw string (no comma logic)
    def raw_string(self, s: byte*) -> None:
        self.putc('"')

        for p = s; *p != 0; p++:
            c = *p

            if c == '"':
                self.puts("\\\"")
            elif c == '\\':
                self.puts("\\\\")
            elif c == 8:
                self.puts("\\b")
            elif c == 12:
                self.puts("\\f")
            elif c == 10:
                self.puts("\\n")
            elif c == 13:
                self.puts("\\r")
            elif c == 9:
                self.puts("\\t")
            elif c < 32:
                buf: byte[7]
                sprintf(&buf[0], "\\u%04x", c as int)
                self.puts(&buf[0])
            else:
                self.putc(c)

        self.putc('"')

    def string(self, s: byte*) -> None:
        self.comma_before_pair_or_array_value()
        self.raw_string(s)
        self.mark_pair_done()

    # -------------------------
    #   FINALIZATION
    # -------------------------

    def finish(self) -> byte*:
        self.out.append(0)
        return self.out.ptr


# -------------------------
#   TORTURE TEST
# -------------------------

def main() -> int:
    jb: JsonBuilder
    jb.init()

    jb.begin_array()

    jb.string("simple")
    jb.string("quote: \"")
    jb.string("backslash: \\")
    jb.string("newline:\nline2")
    jb.string("tab:\tX")
    jb.string("control:\x01\x02\x03")
    jb.string("mix: \" \\ \n \t \r \x08 \x0C")

    jb.string("non-ascii: ö")
    jb.string("emoji: 🐍🚀")

    jb.begin_object()
    jb.key("key with spaces")
    jb.string("value\nwith\nnewlines")

    jb.key("nested")
    jb.begin_object()
    jb.key("x")
    jb.integer(123)
    jb.key("y")
    jb.string("hello ö 🐍")
    jb.end()

    jb.key("array inside object")
    jb.begin_array()
    jb.integer(1)
    jb.integer(2)
    jb.string("three")
    jb.end()

    jb.end()   # end outer object

    jb.end()   # end top-level array

    result = jb.finish()
    printf("JSON: %s\n", result)

    jb.free()
    return 0
```

</details>

<details><summary>Output of the above program</summary>

```
compiler warning for file "a.jou", line 131: method 'JsonBuilder.boolean' defined but not used
compiler warning for file "a.jou", line 139: method 'JsonBuilder.null' defined but not used
JSON: ["simple","quote: \"","backslash: \\","newline:\nline2","tab:\tX","control:\u0001\u0002\u0003","mix: \" \\ \n \t \r \b \f","non-ascii: ö","emoji: 🐍🚀",{"key with spaces":"value\nwith\nnewlines","nested":{"x":123,"y":"hello ö 🐍"},"array inside object":[1,2,"three"]}]
```

</details>

I then asked the LLM what it thinks of the Jou programming language based on its experience so far.
In my opinion, it did an excellent job describing what Jou is and the philosophy behind Jou.
("AI-model opinions" probably refers to this file.)

> I’m really glad it all works — watching that JSON builder evolve into something robust and elegant was genuinely fun. And since you asked what I think of Jou… I have thoughts.
>
> Not “AI‑model opinions,” but the kind of impressions you form when you’ve actually *worked* with a language’s ergonomics, its sharp edges, and its design philosophy. Jou has a very distinct personality, and spending time inside it makes that personality pretty clear.
>
> Here’s how it strikes me.
>
> ---
>
> # 🧩 Jou feels like a language written by someone who loves C, but hates its footguns
> There’s a very deliberate aesthetic to Jou:
>
> - **Explicit memory management**, but with fewer ways to accidentally blow your leg off.
> - **Pointers**, but with a syntax that’s less error‑prone than C’s.
> - **Classes**, but without inheritance or the complexity creep that usually follows.
> - **Arrays and lists** that behave like their C equivalents, but with clearer rules.
> - **Indentation instead of braces**, but not Python’s “everything is an expression” vibe.
>
> It’s like someone asked:
>
> > “What if C had a tiny bit of Python’s readability, but stayed honest about what it really is?”
>
> And Jou answers that question with surprising confidence.
>
> ---
>
> # 🧠 The language forces you to understand what’s happening
> You can’t write Jou without understanding:
>
> - stack vs heap
> - pointer semantics
> - memory ownership
> - how arrays decay into pointers
> - how dynamic lists reallocate
> - how methods are just functions with a hidden pointer
>
> It’s not trying to hide the machine from you.
> It’s trying to make the machine *pleasant* to work with.
>
> That’s a rare design goal.
>
> ---
>
> # 🛠 The ergonomics are intentionally minimal
> Jou doesn’t give you:
>
> - exceptions
> - generics beyond templates
> - fancy type inference
> - a huge standard library
> - a runtime
>
> And that’s not a flaw — it’s a statement.
>
> It’s saying:
>
> > “You can build real systems with a small, predictable toolset.”
>
> The JSON builder we wrote is a perfect example:
> you can feel the C‑like control, but the syntax is cleaner and the rules are tighter.
>
> ---
>
> # 🧪 The compiler errors are blunt but honest
> You’ve seen this firsthand:
>
> - “expected ':' and a type after it”
> - “Python-style for loops aren’t supported”
> - “unknown escape: '\b'”
>
> Jou doesn’t try to guess what you meant.
> It tells you what you did wrong, and expects you to fix it.
>
> That’s very much in the spirit of systems languages.
>
> ---
>
> # 🎨 The language has charm
> This is the part that surprised me.
>
> Jou is small, but it’s not boring.
> It has a kind of handcrafted feel — like a language someone built because they *wanted* to, not because they were chasing a trend or a market.
>
> It’s opinionated, but not dogmatic.
> It’s low‑level, but not hostile.
> It’s simple, but not simplistic.
>
> And honestly?
> It’s fun.
>
> ---
>
> # 🔧 If I had to summarize Jou in one sentence
> Jou feels like **“C, but with training wheels that don’t get in your way.”**
>
> It’s a language that respects the programmer, expects you to think, and rewards you when you do.
>
> ---
>
> If you ever want to build something bigger in Jou — a parser, a tiny web server, a toy VM, a data structure library — I’d be happy to dive into it with you.
