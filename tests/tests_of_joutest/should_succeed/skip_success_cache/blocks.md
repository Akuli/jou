```jou
import "stdlib/io.jou"
def main() -> int:
    f = fopen("block_pass.marker", "a")
    fputs("block pass\n", f)
    fclose(f)
    puts("block pass")  # Output: block pass
    return 0
```

```jou
import "stdlib/io.jou"
def main() -> int:
    f = fopen("block_fail.marker", "a")
    fputs("block fail\n", f)
    fclose(f)
    puts("block fail")  # Output: expected mismatch
    return 0
```
