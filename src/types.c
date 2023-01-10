#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jou_compiler.h"

const Type boolType = { .name = "bool", .kind = TYPE_BOOL };
const Type intType = { .name = "int", .kind = TYPE_SIGNED_INTEGER, .data.width_in_bits = 32 };
const Type byteType = { .name = "byte", .kind = TYPE_UNSIGNED_INTEGER, .data.width_in_bits = 8 };
const Type stringType = { .name = "byte*", .kind = TYPE_POINTER, .data.valuetype = (Type *)&byteType };
const Type voidPtrType = { .name = "void*", .kind = TYPE_VOID_POINTER };

Type create_pointer_type(const Type *elem_type, Location error_location)
{
    Type *dup = malloc(sizeof(*dup));
    *dup = *elem_type;
    Type result = { .kind=TYPE_POINTER, .data.valuetype=dup };

    if (strlen(elem_type->name) + 1 >= sizeof result.name)
        fail_with_error(error_location, "type name too long");
    sprintf(result.name, "%s*", elem_type->name);

    return result;
}

Type create_integer_type(int size_in_bits, bool is_signed)
{
    Type t = { .kind = is_signed?TYPE_SIGNED_INTEGER:TYPE_UNSIGNED_INTEGER, .data.width_in_bits=size_in_bits };
    if (size_in_bits == 8 && !is_signed)
        strcpy(t.name, "byte");
    else if (size_in_bits == 32 && is_signed)
        strcpy(t.name, "int");
    else
        assert(0);
    return t;
}

Type copy_type(const Type *t)
{
    Type t2 = *t;

    switch(t->kind) {
    case TYPE_SIGNED_INTEGER:
    case TYPE_UNSIGNED_INTEGER:
    case TYPE_BOOL:
    case TYPE_VOID_POINTER:
        break;

    case TYPE_POINTER:
        t2.data.valuetype = malloc(sizeof(*t2.data.valuetype));
        *t2.data.valuetype = copy_type(t->data.valuetype);
        break;

    case TYPE_STRUCT:
        {
            int n = t2.data.structmembers.count;

            t2.data.structmembers.names = malloc(sizeof(t2.data.structmembers.names[0]) * n);
            memcpy(t2.data.structmembers.names, t->data.structmembers.names, sizeof(t2.data.structmembers.names[0]) * n);

            t2.data.structmembers.types = malloc(sizeof(t2.data.structmembers.types[0]) * n);
            for (int i = 0; i < n; i++)
                t2.data.structmembers.types[i] = copy_type(&t->data.structmembers.types[i]);
        }
    }

    return t2;
}

bool is_integer_type(const Type *t)
{
    return (t->kind == TYPE_SIGNED_INTEGER || t->kind == TYPE_UNSIGNED_INTEGER);
}

bool is_pointer_type(const Type *t)
{
    return (t->kind == TYPE_POINTER || t->kind == TYPE_VOID_POINTER);
}

bool same_type(const Type *a, const Type *b)
{
    if (a->kind != b->kind)
        return false;

    switch(a->kind) {
    case TYPE_BOOL:
    case TYPE_VOID_POINTER:
        return true;
    case TYPE_POINTER:
        return same_type(a->data.valuetype, b->data.valuetype);
    case TYPE_SIGNED_INTEGER:
    case TYPE_UNSIGNED_INTEGER:
        return a->data.width_in_bits == b->data.width_in_bits;
    case TYPE_STRUCT:
        if (a->data.structmembers.count != b->data.structmembers.count)
            return false;
        for (int i = 0; i < a->data.structmembers.count; i++)
            if (!same_type(&a->data.structmembers.types[i], &b->data.structmembers.types[i])
                || strcmp(a->data.structmembers.names[i], b->data.structmembers.names[i]))
            {
                return false;
            }
        return true;
    }

    assert(0);
}

Type type_of_constant(const Constant *c)
{
    switch(c->kind) {
    case CONSTANT_NULL:
        return voidPtrType;
    case CONSTANT_STRING:
        return stringType;
    case CONSTANT_BOOL:
        return boolType;
    case CONSTANT_INTEGER:
        return create_integer_type(c->data.integer.width_in_bits, c->data.integer.is_signed);
    }
    assert(0);
}


char *signature_to_string(const Signature *sig, bool include_return_type)
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

Signature copy_signature(const Signature *sig)
{
    Signature result = *sig;

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
