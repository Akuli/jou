# Type Checking

Type checking means figuring out what type each thing in a Jou program is,
whether the code tries to access something that doesn't exist,
and also evaluating some values such as `const` statements and array sizes.

For source code, see [the `compiler/typecheck` directory](../../compiler/typecheck/).


## Data Structures (or the lack thereof)

Type-checking results are mostly stored directly into the AST.
While this may seem dirty to some programmers, it is the simplest way to do this I know.
For example, there is no mapping where the keys are AST expressions and the values are their types;
that information is stored directly into the `AstExpression` data structure.


## Steps

(The explanation below talks about function bodies, but the same applies to bodies of methods.)

Type-checking is split into a few steps mostly because
going through the bodies of functions and methods is quite different from everything else.
When going through the body of a function,
the compiler doesn't discover new types or functions or global constants:
all that is handled before looking inside any function bodies at all.

Currently there are three type-checking steps, but in the future there might be just two:
first everything else, then function and method bodies.

This separation also helps with recursive functions.
For example, suppose that function `A()` calls function `bar()`,
and function `bar()` calls function `A()`.
Before realizing that, the compiler checks the **signatures** of these functions.
In other words, it figures out what parameters they take and what they return.
When it goes through the bodies and sees the calls,
it only needs the signatures to check whether the functions are being called correctly,
so it can simply check one function body at a time without caring about bodies of other functions.

To better understand what each step does, see the comments at the start of
[compiler/typecheck/step1_create_types.jou](../../compiler/typecheck/step1_create_types.jou),
[compiler/typecheck/step2_populate_types.jou](../../compiler/typecheck/step2_populate_types.jou) and
[compiler/typecheck/step3_function_and_method_bodies.jou](../../compiler/typecheck/step3_function_and_method_bodies.jou).


## Cyclic and out-of-order definitions

Cyclic and out-of-order references are handled by
checking anything that isn't yet checked as needed
and marking what is currently being checked.

For example, consider the following Jou file:

```python
const A: int = B
const B: int = C  # Error: type 'B' depends on itself
const C: int = B
```

Here's what happens when the compiler tries to compile this file:

1. The compiler sees `const A = B`.
2. Processing the first `const` statement begins. It is marked as being currently processed.
3. The compiler looks for `B` and finds `const B = C`.
4. Processing the second `const` statement begins. It is marked as being currently processed.
5. The compiler looks for `C` and finds `const C = B`.
6. Processing the third `const` statement begins. It is marked as being currently processed.
7. The compiler looks for `B` and finds `const B = C`.
8. Processing the second `const` statement is about to begin again,
    but the compiler notices that it is already being processed,
    and it shows the error message `type 'B' depends on itself`.

The same mechanism is used for `typedef` statements.
It also allows us to use things defined later in the file.
For example, consider the following:

```python
import "stdlib/io.jou"

const message: String = "hello"
typedef String = byte*

def main() -> int:
    puts(message)  # Output: hello
    return 0
```

Here's how the `const` and `typedef` are handled:

1. The compiler sees `const message: String = "hello"`.
2. Processing the `const` statement begins. It is marked as being currently processed.
3. The compiler looks for `String` and finds `typedef String = byte*`.
4. Processing the `typedef` statement begins. It is marked as being currently processed.
5. The compiler figures out what `byte*` means.
6. Processing the `typedef` statement is done. It is marked as no longer being processed. The compiler now knows what `String` is.
7. Processing the `const` statement completes successfully. It is marked as no longer being processed.
8. The compiler is about to process the `typedef`, because it loops through the file top to bottom,
    but it realizes that it has already been processed and does nothing.


## Evaluating Expressions

As already mentioned, type checking also includes
evaluating the values of `const` statements and some other expressions.
Practically this means that the type checking code
and [compiler/evaluate.jou](../../compiler/evaluate.jou) call each other in various ways.
This may feel dirty, but it is fine in my opinion and does not seem to cause problems in practice.

Examples:
- `const foo: int = 123` evaluates the constant `123` during type checking.
- `const bar: int = foo` evaluates `foo` during type checking,
    and the evaluating code invokes the type checker to figure out what `foo` is.


## Imports

To make sure that importing anything `@public`-decorated works smoothly,
the compiler handles imported symbols very similarly to symbols defined in the same file:
it just does things with the `AstStatement` data structures.
The only difference is that imported `AstStatement` data structures
come from parsing the imported file instead of the current file.
For most things, this makes no difference.

To make handling imports even easier in the type checker,
the compiler gathers all `@public`-decorated statements from all imported files
into a list before type checking beings.
There is one list per source file.
The type checker then walks through the list when it is trying to find where something is defined.
This is simpler than finding all `import` statements,
finding the ASTs for the imported files,
and finally going through everything `@public` in those ASTs.


## Performance

LLVM is so slow that type checking performance generally doesn't matter.
For example, there are no hash tables and everything is looked up from lists in linear time.
It goes without saying that if performance becomes a problem in the future, this may need to change.
