# How does the compiler work?

Suppose you have file `foo.jou` with the following content:

```python3
import "./bar.jou"
import "stdlib/io.jou"

def main() -> int:
    printf("Foo\n")
    bar()
    return 0
```

And file `bar.jou` with this content:

```python3
import "stdlib/io.jou"

@public
def bar() -> None:
    printf("Bar\n")
```

The following picture shows what happens when you run `jou foo.jou`:



At a high level, the compilation steps are:
- **Tokenize:** split the source code into tokens
- **Parse:** build an abstract syntax tree (AST) from the tokens
- **Typecheck:** errors for wrong number or type of function arguments etc, figure out the type of each expression in the AST
- **Build LLVM IR:** walk the AST and call methods on a builder that builds LLVM IR (see [compiler/builders/](compiler/builders/))
- **Emit objects:** create `.o` files from the LLVM IR
- **Link:** run a linker that combines the `.o` files into an executable
- **Run:** run the executable

To get a good idea of how these steps work,
you can run the compiler in verbose mode:

```
$ ./jou -v examples/hello.jou   # High-level overview
$ ./jou -vv examples/hello.jou  # Show all details
```

With `-vv` (or `--verbose --verbose`), the compiler shows
the tokens, AST and LLVM IR generated.
LLVM IR is shown twice, before and after optimizing.

After making changes to the compiler, run `make` to recompile it.
