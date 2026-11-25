Three big parts:
- langserver
- analyzer
- compiler backend

Structure:

```
langserver/
    lsp_main.jou
    lsp_state.jou
    lsp_handlers.jou
    ...
compiler_backend/
    paths.jou
    builder.jou
    llvm.jou
    pthreads.jou
    run.jou
    ...
analyzer/
    ast.jou
    parser.jou
    tokenizer.jou
    evaluate.jou
    ...
vfs.jou     # VFS is created outside analyzer. Creation options: expiration timeout, assert no actual reads.
hash.jou
main.jou
update.jou
```
