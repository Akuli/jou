#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jou_compiler.h"

struct TypeInfo {
    Type type;
    struct TypeInfo *pointer;  // type that represents a pointer to this type, or NULL
    List(struct TypeInfo *) arrays;  // types that represent arrays of this type
};

static struct {
    bool inited;
    struct TypeInfo integers[65][2];  // integers[i][j] = i-bit integer, j=1 for signed, j=0 for unsigned
    struct TypeInfo boolean, doublelele, floater, voidptr;
} global_state = {0};

const Type *boolType = &global_state.boolean.type;
const Type *intType = &global_state.integers[32][true].type;
const Type *longType = &global_state.integers[64][true].type;
const Type *byteType = &global_state.integers[8][false].type;
const Type *sizeType = &global_state.integers[8*sizeof(size_t)][false].type;
const Type *floatType = &global_state.floater.type;
const Type *doubleType = &global_state.doublelele.type;
const Type *voidPtrType = &global_state.voidptr.type;

// The TypeInfo for type T contains the type T* (if it has been used)
// and all array types with element type T.
static void free_pointer_and_array_types(const struct TypeInfo *info)
{
    free_type(&info->pointer->type);
    for (struct TypeInfo **arrtype = info->arrays.ptr; arrtype < End(info->arrays); arrtype++)
        free_type(&(*arrtype)->type);
    free(info->arrays.ptr);
}

void free_type(Type *t)
{
    if (t) {
        if (t->kind == TYPE_CLASS) {
            for (const Signature *m = t->data.classdata.methods.ptr; m < End(t->data.classdata.methods); m++)
                free_signature(m);
            free(t->data.classdata.fields.ptr);
            free(t->data.classdata.methods.ptr);
        }
        assert(offsetof(struct TypeInfo, type) == 0);
        free_pointer_and_array_types((struct TypeInfo *)t);
        free(t);
    }
}

static void free_global_state(void)
{
    assert(global_state.inited);
    free_pointer_and_array_types(&global_state.boolean);
    free_pointer_and_array_types(&global_state.floater);
    free_pointer_and_array_types(&global_state.doublelele);
    free_pointer_and_array_types(&global_state.voidptr);
    for (int size = 8; size <= 64; size *= 2)
        for (int is_signed = 0; is_signed <= 1; is_signed++)
            free_pointer_and_array_types(&global_state.integers[size][is_signed]);
}

void init_types(void)
{
    assert(!global_state.inited);

    global_state.boolean.type = (Type){ .name = "bool", .kind = TYPE_BOOL };
    global_state.voidptr.type = (Type){ .name = "void*", .kind = TYPE_VOID_POINTER };
    global_state.floater.type = (Type){ .name = "float", .kind = TYPE_FLOATING_POINT, .data.width_in_bits = 32 };
    global_state.doublelele.type = (Type){ .name = "double", .kind = TYPE_FLOATING_POINT, .data.width_in_bits = 64 };

    for (int size = 8; size <= 64; size *= 2) {
        global_state.integers[size][true].type.kind = TYPE_SIGNED_INTEGER;
        global_state.integers[size][false].type.kind = TYPE_UNSIGNED_INTEGER;

        for (int is_signed = 0; is_signed <= 1; is_signed++) {
            global_state.integers[size][is_signed].type.data.width_in_bits = size;
            sprintf(
                global_state.integers[size][is_signed].type.name,
                "<%d-bit %s integer>",
                size, is_signed?"signed":"unsigned");
        }
    }

    strcpy(global_state.integers[8][false].type.name, "byte");
    strcpy(global_state.integers[32][true].type.name, "int");
    strcpy(global_state.integers[64][true].type.name, "long");
    strcpy(global_state.integers[sizeof(size_t)*8][false].type.name, "size_t");

    global_state.inited = true;
    atexit(free_global_state);  // not really necessary, but makes valgrind happier
}

const Type *get_integer_type(int size_in_bits, bool is_signed)
{
    assert(size_in_bits==8 || size_in_bits==16 || size_in_bits==32 || size_in_bits==64);
    return &global_state.integers[size_in_bits][is_signed].type;
}

const Type *get_pointer_type(const Type *t)
{
    assert(offsetof(struct TypeInfo, type) == 0);
    struct TypeInfo *info = (struct TypeInfo *)t;

    if (!info->pointer) {
        struct TypeInfo *ptr = calloc(1, sizeof *ptr);
        ptr->type = (Type){ .kind=TYPE_POINTER, .data.valuetype=t };
        snprintf(ptr->type.name, sizeof ptr->type.name, "%s*", t->name);
        info->pointer = ptr;
    }
    return &info->pointer->type;
}

const Type *get_array_type(const Type *t, int len)
{
    assert(offsetof(struct TypeInfo, type) == 0);
    struct TypeInfo *info = (struct TypeInfo *)t;

    assert(len > 0);
    for (struct TypeInfo **existing = info->arrays.ptr; existing < End(info->arrays); existing++)
        if ((*existing)->type.data.array.len == len)
            return &(*existing)->type;

    struct TypeInfo *arr = calloc(1, sizeof *arr);
    arr->type = (Type){ .kind = TYPE_ARRAY, .data.array.membertype = t, .data.array.len = len };
    snprintf(arr->type.name, sizeof arr->type.name, "%s[%d]", t->name, len);
    Append(&info->arrays, arr);
    return &arr->type;
}

bool is_integer_type(const Type *t)
{
    return (t->kind == TYPE_SIGNED_INTEGER || t->kind == TYPE_UNSIGNED_INTEGER);
}

bool is_number_type(const Type *t)
{
      return is_integer_type(t) || t->kind == TYPE_FLOATING_POINT;
}

bool is_pointer_type(const Type *t)
{
    return (t->kind == TYPE_POINTER || t->kind == TYPE_VOID_POINTER);
}

const Type *type_of_constant(const Constant *c)
{
    switch(c->kind) {
    case CONSTANT_ENUM_MEMBER:
        return c->data.enum_member.enumtype;
    case CONSTANT_NULL:
        return voidPtrType;
    case CONSTANT_DOUBLE:
        return doubleType;
    case CONSTANT_FLOAT:
        return floatType;
    case CONSTANT_BOOL:
        return boolType;
    case CONSTANT_STRING:
        return get_pointer_type(byteType);
    case CONSTANT_INTEGER:
        return get_integer_type(c->data.integer.width_in_bits, c->data.integer.is_signed);
    }
    assert(0);
}

Type *create_opaque_struct(const char *name)
{
    struct TypeInfo *result = calloc(1, sizeof *result);
    result->type = (Type){ .kind = TYPE_OPAQUE_CLASS };

    assert(strlen(name) < sizeof result->type.name);
    strcpy(result->type.name, name);

    return &result->type;
}

Type *create_enum(const char *name, int membercount, char (*membernames)[100])
{
    struct TypeInfo *result = calloc(1, sizeof *result);
    result->type = (Type){
        .kind = TYPE_ENUM,
        .data.enummembers = { .count=membercount, .names=membernames },
    };

    assert(strlen(name) < sizeof result->type.name);
    strcpy(result->type.name, name);

    return &result->type;
}


const Type *get_self_class(const Signature *sig)
{
    if (sig->nargs > 0 && !strcmp(sig->argnames[0], "self")) {
        assert(sig->argtypes[0]->kind == TYPE_POINTER);
        return sig->argtypes[0]->data.valuetype;
    }
    return NULL;
}

char *signature_to_string(const Signature *sig, bool include_return_type)
{
    List(char) result = {0};
    AppendStr(&result, sig->name);
    Append(&result, '(');

    for (int i = 0; i < sig->nargs; i++) {
        if(i)
            AppendStr(&result, ", ");
        AppendStr(&result, sig->argnames[i]);
        AppendStr(&result, ": ");
        AppendStr(&result, sig->argtypes[i]->name);
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

    result.argtypes = malloc(sizeof(result.argtypes[0]) * result.nargs); // NOLINT
    for (int i = 0; i < result.nargs; i++)
        result.argtypes[i] = sig->argtypes[i];

    result.argnames = malloc(sizeof(result.argnames[0]) * result.nargs);
    memcpy(result.argnames, sig->argnames, sizeof(result.argnames[0]) * result.nargs);

    return result;
}
