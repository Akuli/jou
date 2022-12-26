#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jou_compiler.h"

const struct Type boolType = { .name = "bool", .kind = TYPE_BOOL };
const struct Type intType = { .name = "int", .kind = TYPE_SIGNED_INTEGER, .data.width_in_bits = 32 };
const struct Type byteType = { .name = "byte", .kind = TYPE_UNSIGNED_INTEGER, .data.width_in_bits = 8 };
const struct Type stringType = { .name = "byte*", .kind = TYPE_POINTER, .data.valuetype = (struct Type *)&byteType };

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

bool same_type(const struct Type *a, const struct Type *b)
{
    assert(a->kind != TYPE_UNKNOWN && b->kind != TYPE_UNKNOWN);

    if (a->kind != b->kind)
        return false;

    switch(a->kind) {
    case TYPE_BOOL:
        return true;
    case TYPE_POINTER:
        return same_type(a->data.valuetype, b->data.valuetype);
    case TYPE_SIGNED_INTEGER:
    case TYPE_UNSIGNED_INTEGER:
        return a->data.width_in_bits == b->data.width_in_bits;
    case TYPE_UNKNOWN:
        assert(0);
    }

    assert(0);
}

// This should be kept in sync with codegen.c because it's what actually does the conversions.
bool can_implicitly_convert(const struct Type *from, const struct Type *to)
{
    assert(from->kind != TYPE_UNKNOWN && to->kind != TYPE_UNKNOWN);

    if (from->kind == TYPE_UNSIGNED_INTEGER && to->kind == TYPE_SIGNED_INTEGER) {
        // The only implicit conversion between different kinds of types.
        // Can't be done with types of same size: e.g. with 8 bits, 255 does not implicitly convert to -1.
        return from->data.width_in_bits < to->data.width_in_bits;
    }

    if (from->kind != to->kind)
        return false;

    switch(from->kind) {
    case TYPE_BOOL:
        return true;
    case TYPE_POINTER:
        return same_type(from->data.valuetype, to->data.valuetype);
    case TYPE_SIGNED_INTEGER:
    case TYPE_UNSIGNED_INTEGER:
        return from->data.width_in_bits <= to->data.width_in_bits;
    case TYPE_UNKNOWN:
        assert(0);
    }

    assert(0);
}
