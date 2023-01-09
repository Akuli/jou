#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jou_compiler.h"

const struct Type boolType = { .name = "bool", .kind = TYPE_BOOL };
const struct Type intType = { .name = "int", .kind = TYPE_SIGNED_INTEGER, .data.width_in_bits = 32 };
const struct Type byteType = { .name = "byte", .kind = TYPE_UNSIGNED_INTEGER, .data.width_in_bits = 8 };
const struct Type stringType = { .name = "byte*", .kind = TYPE_POINTER, .data.valuetype = (struct Type *)&byteType };
const struct Type voidPtrType = { .name = "void*", .kind = TYPE_VOID_POINTER };

struct Type create_pointer_type(const struct Type *elem_type, struct Location error_location)
{
    struct Type *dup = malloc(sizeof(*dup));
    *dup = *elem_type;
    struct Type result = { .kind=TYPE_POINTER, .data.valuetype=dup };

    if (strlen(elem_type->name) + 1 >= sizeof result.name)
        fail_with_error(error_location, "type name too long");
    sprintf(result.name, "%s*", elem_type->name);

    return result;
}

struct Type create_integer_type(int size_in_bits, bool is_signed)
{
    struct Type t = { .kind = is_signed?TYPE_SIGNED_INTEGER:TYPE_UNSIGNED_INTEGER, .data.width_in_bits=size_in_bits };
    if (size_in_bits == 8 && !is_signed)
        strcpy(t.name, "byte");
    else if (size_in_bits == 32 && is_signed)
        strcpy(t.name, "int");
    else
        assert(0);
    return t;
}

struct Type copy_type(const struct Type *t)
{
    assert(t);

    switch(t->kind) {
    case TYPE_SIGNED_INTEGER:
    case TYPE_UNSIGNED_INTEGER:
    case TYPE_BOOL:
    case TYPE_VOID_POINTER:
        return *t;
    case TYPE_POINTER:
        {
            struct Type t2 = *t;
            if (t->data.valuetype) {
                // not a void pointer
                t2.data.valuetype = malloc(sizeof(*t2.data.valuetype));
                *t2.data.valuetype = copy_type(t->data.valuetype);
            }
            return t2;
        }
    }
    assert(0);
}

bool is_integer_type(const struct Type *t)
{
    return (t->kind == TYPE_SIGNED_INTEGER || t->kind == TYPE_UNSIGNED_INTEGER);
}

bool same_type(const struct Type *a, const struct Type *b)
{
    assert(a);
    assert(b);

    if (a->kind != b->kind)
        return false;

    switch(a->kind) {
    case TYPE_BOOL:
    case TYPE_VOID_POINTER:
        return true;
    case TYPE_POINTER:
    {
        struct Type *t1 = a->data.valuetype, *t2 = b->data.valuetype;
        return (!t1 && !t2) || (t1 && t2 && same_type(t1, t2));
    }
    case TYPE_SIGNED_INTEGER:
    case TYPE_UNSIGNED_INTEGER:
        return a->data.width_in_bits == b->data.width_in_bits;
    }

    assert(0);
}


char *signature_to_string(const struct Signature *sig, bool include_return_type)
{
    List(char) result = {0};
    AppendStr(&result, sig->funcname);
    Append(&result, '(');

    for (int i = 0; i < sig->nargs; i++) {
        if(i)
            AppendStr(&result, ", ");
        AppendStr(&result, sig->argnames[i]);
        AppendStr(&result, ": ");
        AppendStr(&result, sig->argtypes[i].name);
    }
    if (sig->takes_varargs) {
        if (sig->nargs)
            AppendStr(&result, ", ");
        AppendStr(&result, "...");
    }
    Append(&result, ')');
    if (include_return_type) {
        AppendStr(&result, " -> ");
        AppendStr(&result, sig->returntype ? sig->returntype->name : "void");
    }
    Append(&result, '\0');
    return result.ptr;
}

struct Signature copy_signature(const struct Signature *sig)
{
    struct Signature result = *sig;

    result.argtypes = malloc(sizeof(result.argtypes[0]) * result.nargs);
    for (int i = 0; i < result.nargs; i++)
        result.argtypes[i] = copy_type(&sig->argtypes[i]);

    result.argnames = malloc(sizeof(result.argnames[0]) * result.nargs);
    memcpy(result.argnames, sig->argnames, sizeof(result.argnames[0]) * result.nargs);

    if (result.returntype) {
        result.returntype = malloc(sizeof(*result.returntype));
        *result.returntype = copy_type(sig->returntype);
    }

    return result;
}
