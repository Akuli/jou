#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jou_compiler.h"

struct Type create_pointer_type(const struct Type *elem_type, struct Location error_location)
{
    assert(elem_type->kind != TYPE_UNKNOWN);

    struct Type *dup = malloc(sizeof(*dup));
    *dup = *elem_type;
    struct Type result = { .kind=TYPE_POINTER, .data.valuetype=dup };

    if (strlen(elem_type->name) + 1 >= sizeof result.name)
        fail_with_error(error_location, "type name too long");
    sprintf(result.name, "%s*", elem_type->name);

    return result;
}

bool types_match(const struct Type *a, const struct Type *b)
{
    assert(a->kind != TYPE_UNKNOWN && b->kind != TYPE_UNKNOWN);

    if (a->kind != b->kind)
        return false;

    switch(a->kind) {
    case TYPE_BOOL:
        return true;
    case TYPE_POINTER:
        return types_match(a->data.valuetype, b->data.valuetype);
    case TYPE_SIGNED_INTEGER:
    case TYPE_UNSIGNED_INTEGER:
        return a->data.width_in_bits == b->data.width_in_bits;
    case TYPE_UNKNOWN:
        assert(0);
    }

    assert(0);
}
