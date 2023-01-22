#ifndef UTIL_H
#define UTIL_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define min(a,b) ((a)<(b) ? (a) : (b))
#define max(a,b) ((a)>(b) ? (a) : (b))

/*
List(T) is a dynamically growing array, similar to Python's list data type. Example:

    // Making a new, empty list. The {0} is needed for lists of all types.
    List(int) nums = {0};

    // Add numbers to it.
    Append(&nums, 1);
    Append(&nums, 2);
    Append(&nums, 3);

    // Print each number with pointers.
    for (int *p = nums.ptr; p < End(nums); p++)
        printf("%d\n", *p);

    // Print each number with indexes.
    for (int i = 0; i < nums.len; i++)
        printf("%d\n", nums.ptr[i]);

    // Free the memory used by the list.
    free(nums.ptr);

Gotchas to watch out for:

- Every occurrence of List(T) is a new, incompatible type, so you can't
  use List in function arguments. If you really need to, make a typedef of
  List(SomeType) or wrap it in a struct.

- The elements can get reallocated in Append(). This messes up all pointers
  to the list, including any loops that don't use indexes.

- Do NOT do this:

        for (Foo *thing = End(list) - 1; thing >= list.ptr; thing--) {
            ...use thing...
        }

  It can fail if the list is empty, because End(list) can be NULL. Use indexes
  if you need to loop backwards:

        for (int i = list.len - 1; i >= 0; i--) {
            ...use list.ptr[i] ...
        }

- Side effects of foo() in Append(list, foo()) must not modify the list that is
  being appended into. It creates confusing bugs. You may want to store the
  result of foo() into a variable before calling Append().
*/
#define List(T) struct { T *ptr; int len,alloc; }
#define Append(list, ...) do { \
    if ((list)->alloc == (list)->len) { \
        if ((list)->alloc==0) (list)->alloc=1; \
        (list)->alloc*=2; \
        (list)->ptr=realloc((list)->ptr,sizeof((list)->ptr[0])*(list)->alloc); /* NOLINT */ \
        if (!(list)->ptr) { \
            fprintf(stderr, "out of memory\n"); \
            exit(1); \
        } \
    } \
    (list)->ptr[(list)->len++]=(__VA_ARGS__); \
} while(0)
#define End(list) (&(list).ptr[(list).len])
#define Pop(list) ( assert((list)->len > 0), (list)->ptr[--(list)->len] )

// list should be a List(char). See above for why this can't be a function.
#define AppendStr(list,str) do{ \
    const char *appendstr_s = (str); \
    while(*appendstr_s) Append((list),*appendstr_s++); \
} while(0)

// strcpy between two char arrays is safe, if there is enough room.
// Not intended to replace all uses of strcpy(), only array-to-array copying.
#define safe_strcpy(dest, src) do{ \
    static_assert(sizeof(src) > sizeof(char*), "src must be an array, not a pointer"); \
    static_assert(sizeof(dest) > sizeof(char*), "dest must be an array, not a pointer"); \
    static_assert(sizeof(dest) >= sizeof(src), "not enough room in dest"); \
    strcpy((dest),(src)); \
} while(0)

// Return a full path to the currently running executable.
char *find_current_executable(void);

#endif
