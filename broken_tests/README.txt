Tests in this directory fail if they are moved into the tests folder.
This is because the self-hosted compiler doesn't analyze the CFGs.

The analysis code exists, but it's written in C, because it used to be a
part of what's now the bootstrap compiler. It's in simplify_cfg.c.

See issue #566.
