Tests in this directory fail if they are moved into the tests folder.
This is because the compiler doesn't analyze the CFGs.

The analysis code exists, but it's written in C, because it used to be a
part of the old Jou compiler written in C. It's in simplify_cfg.c.

See issue #566.
