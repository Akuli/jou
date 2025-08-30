from __future__ import annotations

import itertools
import faulthandler
import functools
import subprocess
import shutil
import sys
import os
import string
import ctypes.util
import re
from dataclasses import dataclass
from typing import Any, TYPE_CHECKING, Iterator

if TYPE_CHECKING:
    # First element is always a string, but explainin that to mypy without
    # getting unnecessary errors turned out to be too complicated.
    AST = tuple[Any, ...]


# GLOBAL STATE GO BRRRR
# This place is where all mutated global variables go.
VERBOSITY: int = 0
LIBS: list[ctypes.CDLL] = []
hey_python_please_dont_garbage_collect_these_strings_thank_you = []  # TODO: remove?
ASTS: dict[str, Any] = {}
FUNCTIONS: dict[str, dict[str, Any]] = {}
TYPES: dict[str, dict[str | tuple[str, tuple[JouType, ...] | JouType], JouType]] = {}
GLOBALS: dict[str, dict[str, JouValue | None]] = {}  # None means not found
CONSTANTS: dict[str, dict[str, JouValue | None]] = {}  # None means not found
ENUMS: dict[str, dict[str, list[str] | None]] = {}  # None means not found
CALL_STACK: list[str] = []  # This is only for debugging.


def load_llvm():
    # TODO: windows
    assert sys.platform != "win32"

    llvm_config = (
        os.environ.get("LLVM_CONFIG")
        or shutil.which("llvm-config-19")
        or shutil.which("/usr/local/opt/llvm@19/bin/llvm-config")
        or shutil.which("/opt/homebrew/opt/llvm@19/bin/llvm-config")
        or shutil.which("llvm-config-18")
        or shutil.which("/usr/local/opt/llvm@18/bin/llvm-config")
        or shutil.which("/opt/homebrew/opt/llvm@18/bin/llvm-config")
        or shutil.which("llvm-config-17")
        or shutil.which("/usr/local/opt/llvm@17/bin/llvm-config")
        or shutil.which("/opt/homebrew/opt/llvm@17/bin/llvm-config")
        or shutil.which("llvm-config-16")
        or shutil.which("/usr/local/opt/llvm@16/bin/llvm-config")
        or shutil.which("/opt/homebrew/opt/llvm@16/bin/llvm-config")
        or shutil.which("llvm-config-15")
        or shutil.which("/usr/local/opt/llvm@15/bin/llvm-config")
        or shutil.which("/opt/homebrew/opt/llvm@15/bin/llvm-config")
        or shutil.which("llvm-config")
    )
    if not llvm_config:
        sys.exit("Error: llvm-config not found")

    # Check LLVM version
    v = subprocess.check_output([llvm_config, "--version"], text=True).strip()
    if not v.startswith(("15.", "16.", "17.", "18.", "19.")):
        sys.exit(
            f"Error: Found unsupported LLVM version {v}. Only LLVM 15,16,17,18,19 are supported."
        )

    result = []
    for path in subprocess.check_output(
        [llvm_config, "--libfiles"], text=True
    ).splitlines():
        if VERBOSITY >= 1:
            print("Found LLVM:", path)
        result.append(ctypes.CDLL(path))

    return result


@dataclass
class Token:
    kind: str
    code: str
    location: tuple[str, int]


def tokenize(jou_code, path):
    regex = r"""
    (?P<newline> \n [ ]* )                      # a newline and all indentation after it
    | (?P<keyword>
        \bimport \b
        | \b link \b
        | \b def \b
        | \b declare \b
        | \b class \b
        | \b union \b
        | \b enum \b
        | \b global \b
        | \b const \b
        | \b return \b
        | \b if \b
        | \b elif \b
        | \b else \b
        | \b while \b
        | \b for \b
        | \b pass \b
        | \b break \b
        | \b continue \b
        | \b True \b
        | \b False \b
        | \b None \b
        | \b NULL \b
        | \b void \b
        | \b noreturn \b
        | \b and \b
        | \b or \b
        | \b not \b
        | \b self \b
        | \b as \b
        | \b sizeof \b
        | \b assert \b
        | \b bool \b
        | \b byte \b
        | \b int \b
        | \b long \b
        | \b float \b
        | \b double \b
        | \b match \b
        | \b with \b
        | \b case \b
    )
    | (?P<name> [^\W\d] \w* )                   # "foo"
    | (?P<decorator> @public | @inline )
    | (?P<string> " ( \\ . | [^"\n\\] )* " )    # "hello"
    | (?P<byte> ' ( \\ . | . ) ' )              # 'a' or '\n'
    | (?P<op> \.\.\. | == | != | -> | <= | >= | \+\+ | -- | [+*/%&^|-]= | [.,:;=(){}\[\]&%*/+^<>|-] )
    | (?P<double>
        [0-9]+ \. [0-9]+                        # 1.23 or .123
        | (?:                                   # or:
            [0-9]+ \. [0-9]+                        # 1.23
            | [0-9]                                 # or 123
        ) e -? [0-9]+                               # followed by e3 or e-5
    )
    | (?P<int>
        0b [01_]+                               # 0b0100_1010
        | 0o [0-7_]+                            # 0o777
        | 0x [0-9A-Fa-f_]+                      # 0xFFFF_FFFF
        | [0-9] [0-9_]*                         # 123
    )
    | (?P<ignore> \# .* | [ ] )                 # comments and spaces other than indentation
    | (?P<error> [\S\s] )                       # anything else is an error
    """

    tokens = []
    lineno = 1

    for m in re.finditer(regex, jou_code, flags=re.VERBOSE):
        if m.lastgroup == "error":
            exit(f"tokenize error in file {path}, line {lineno}")

        if m.lastgroup != "ignore":
            assert m.lastgroup is not None
            tokens.append(
                Token(
                    kind=m.lastgroup.rstrip("_"),
                    code=m.group(0),
                    location=(path, lineno),
                )
            )

        lineno += m.group(0).count("\n")

    # Ignore all whitespace inside parentheses
    paren_depth = 0
    i = 0
    while i < len(tokens):
        if tokens[i].code in {"(", "{", "["}:
            paren_depth += 1
        if tokens[i].code in {")", "}", "]"}:
            paren_depth -= 1

        if tokens[i].kind == "newline" and paren_depth > 0:
            del tokens[i]
        else:
            i += 1
    assert paren_depth == 0, f"unbalanced parentheses in file {path}"

    # If repeated newlines, ignore that
    i = 0
    while i + 1 < len(tokens):
        if tokens[i].kind == "newline" and tokens[i + 1].kind == "newline":
            del tokens[i]
        else:
            i += 1

    # Ignore newlines in the beginning
    while tokens and tokens[0].kind == "newline":
        del tokens[0]

    # Insert "indent" and "dedent" tokens where indentation level changes
    indent_level = 0
    i = 0
    while i < len(tokens):
        if tokens[i].kind != "newline":
            i += 1
            continue
        newline = tokens[i]
        n = len(newline.code.strip("\n"))
        newline.code = "\n"
        i += 1
        assert n % 4 == 0
        while indent_level < n:
            tokens.insert(i, Token("indent", "    ", (path, newline.location[1] + 1)))
            indent_level += 4
            i += 1
        while indent_level > n:
            tokens.insert(i, Token("dedent", "", (path, newline.location[1] + 1)))
            indent_level -= 4
            i += 1

    return list(tokens)


# reverse code golfing: https://xkcd.com/1960/
def determine_the_kind_of_a_statement_that_starts_with_an_expression(t):
    if t.code == "=":
        return "assign"
    if t.code == "+=":
        return "in_place_add"
    if t.code == "-=":
        return "in_place_sub"
    if t.code == "*=":
        return "in_place_mul"
    if t.code == "/=":
        return "in_place_div"
    if t.code == "%=":
        return "in_place_mod"
    if t.code == "&=":
        return "in_place_bit_and"
    if t.code == "|=":
        return "in_place_bit_or"
    if t.code == "^=":
        return "in_place_bit_xor"
    return "expr_stmt"


# The multiple cases of "case a | b | c | d:" appear as a nested bitwise OR
# expression. Here's what this function does to it:
#
#   BitOr(BitOr(BitOr(a, b), c), d)     -->     [a, b, c, d]
#
def flatten_bitwise_ors(expr):
    if expr[0] == "bit_or":
        _, lhs, rhs = expr
        return flatten_bitwise_ors(lhs) + flatten_bitwise_ors(rhs)
    return [expr]


def unescape_string(s: str) -> bytes:
    result = bytearray()
    assert s[0] == '"'
    assert s[-1] == '"'
    chars = iter(s[1:-1])
    escapes = {
        r"\n": b"\n",
        r"\t": b"\t",
        r"\r": b"\r",
        r"\"": b'"',
        r"\\": b"\\",
    }

    while True:
        try:
            c = next(chars)
        except StopIteration:
            return bytes(result)

        if c == "\\":
            c2 = next(chars)
            if c2 == "x":
                # e.g. \xff
                c3 = next(chars)
                c4 = next(chars)
                result.append(int(c3 + c4, 16))
            else:
                result += escapes[c + c2]
        else:
            result += c.encode("utf-8")


@functools.cache
def simplify_path(path: str) -> str:
    # Remove "foo/../bar" and "foo/./bar" stuff
    return os.path.relpath(os.path.abspath(path)).replace("\\", "/")


class JouType:
    """A type in Jou code."""

    def __init__(self, name: str) -> None:
        self.name = name
        # ctypes_type may be None for now and assigned later.
        # This is needed when a class references itself through pointers.
        self.inner_type: JouType | None = None
        self.class_field_types: dict[str, JouType] | None = None
        self.methods: dict[str, tuple[str, Any]] = {}
        self.generic_params: dict[str, JouType] = {}
        self.funcptr_argtypes: list[JouType] | None = None
        self.funcptr_return_type: JouType | None = None

        self._ctypes_type: Any | None = None
        self._pointer_type: JouType | None = None
        self._array_types: dict[int, JouType] = {}

    def __repr__(self) -> str:
        return f"<JouType: {self.name}>"

    def is_integer_type(self) -> bool:
        # Kinda hacky, but works fine
        return bool(re.fullmatch(r"u?int(8|16|32|64)", self.name))

    def is_array_type(self) -> bool:
        return bool(re.fullmatch(r".*\[[0-9]+\]", self.name))

    def ctypes_type(self) -> Any:
        if self._ctypes_type is not None:
            return self._ctypes_type

        result: Any
        if self.name == "bool":
            result = ctypes.c_uint8
        elif self.name == "float":
            result = ctypes.c_float
        elif self.name == "double":
            result = ctypes.c_double
        elif self.is_integer_type():
            result = getattr(ctypes, "c_" + self.name)
        elif self.name.startswith("funcptr("):
            # All functions are of the same type in ctypes. That is the main reason
            # why we need JouType in the first place.
            assert type(LIBS[0].printf) is type(LIBS[0].strcpy)
            result = type(LIBS[0].printf)
        elif self.name.endswith("*"):
            # Most of the time it's good to use void* for pointers where you
            # don't care about the contents. This helps in various corner cases
            # where classes refer to each other.
            result = ctypes.c_void_p
        elif re.fullmatch(r".*\[[0-9]+\]$", self.name):
            # Array
            array_len = int(self.name.split("[")[-1].strip("]"))
            assert self.inner_type is not None
            result = self.inner_type.ctypes_type() * array_len
        elif self.class_field_types is not None:
            this_is_needed_to_satisfy_mypy = self.class_field_types

            class JouClass(ctypes.Structure):
                _fields_ = [
                    (fname, ftype.ctypes_type())
                    for fname, ftype in this_is_needed_to_satisfy_mypy.items()
                ]

            result = JouClass
        else:
            raise NotImplementedError(self)

        self._ctypes_type = result
        return result

    def pointer_type(self) -> JouType:
        if self._pointer_type is not None:
            return self._pointer_type

        if self.name.startswith("funcptr("):
            name = f"({self.name})*"
        else:
            name = f"{self.name}*"

        result = JouType(name)
        result.inner_type = self
        self._pointer_type = result
        return result

    def array_type(self, length: int) -> JouType:
        # TODO: If self is a funcptr, then self.name needs to be in parentheses
        if length in self._array_types:
            return self._array_types[length]

        if self.name.startswith("funcptr("):
            name = f"({self.name})[{length}]"
        else:
            name = f"{self.name}[{length}]"

        result = JouType(name)
        result.inner_type = self

        self._array_types[length] = result
        return result

    def allocate_instance(self) -> JouValue:
        """Return a pointer to a newly allocated value.

        The ctypes module allocates the memory for the value. I honestly don't
        know how it is cleaned up. Maybe with Python's garbage collection,
        maybe never. In any case it seems to work.
        """
        ctypes_instance = self.ctypes_type()()
        ctypes_ptr = ctypes.pointer(ctypes_instance)
        return JouValue(self.pointer_type(), ctypes_ptr)


class JouValue:
    """An IMMUTABLE value in Jou code.

    When you create a new JouValue, make sure that its ctypes stuff is a copy
    of the actual object, like it would be in Jou's pass by value.

    For example, consider the following Jou code:

        class Foo:
            x: int

        def main() -> int:
            f = Foo{}
            x = f.x
            x += 1
            printf("%d\n", f.x)  # Output: 0
            return 0

    When we do `x = f.x`, we construct a JouValue that represents `f.x`. There
    should be no way to modify the value of `f.x` through this newly construted
    JouValue.
    """

    def __init__(
        self, jou_type: JouType, ctypes_value: Any, *, cast: bool = False
    ) -> None:
        if cast:
            ctypes_value = ctypes.cast(ctypes_value, jou_type.ctypes_type())
        elif jou_type.name.endswith("*") and not jou_type.name.startswith("funcptr("):
            # Cast all pointers to void*
            ctypes_value = ctypes.cast(ctypes_value, ctypes.c_void_p)
        else:
            assert type(ctypes_value) == jou_type.ctypes_type()

        self.jou_type = jou_type
        self.ctypes_value = ctypes_value

    def __repr__(self) -> str:
        try:
            v: str | int = self.unwrap_value()
        except Exception:
            v = "..."
        return f"<JouValue: {self.jou_type.name} {v}>"

    def cast(self, to: JouType) -> JouValue:
        if self.jou_type == to:
            return self

        assert not self.jou_type.name.startswith("funcptr(")
        assert not to.name.startswith("funcptr(")

        if self.jou_type.name.endswith(("*", "]")) and to.name.endswith("*"):
            # Simple pointer-to-pointer cast or array-to-pointer cast
            assert to.ctypes_type is not None
            return JouValue(to, ctypes.cast(self.ctypes_value, to.ctypes_type()))

        if self.jou_type.name == "uint8*" and re.fullmatch(r"uint8\[\d+\]", to.name):
            # String from pointer to array (only allowed in some special cases)
            assert to.ctypes_type is not None
            array = to.ctypes_type()()
            LIBS[0].strcpy(self.ctypes_value, array)
            return JouValue(to, array)

        if self.jou_type.is_integer_type() and to.is_integer_type():
            # Simple integer-to-integer cast.
            return jou_integer(to, self.ctypes_value.value)

        raise RuntimeError(f"cannot cast {self.jou_type.name} to {to.name}")

    def deref(self) -> JouValue:
        """Get the value of a pointer."""
        assert self.jou_type.name.endswith("*"), f"can't deref {self.jou_type.name}"
        assert self.jou_type.inner_type is not None
        assert self.jou_type.inner_type.ctypes_type is not None

        # We store all pointers to void*, let's undo that
        real_ptr_type = ctypes.POINTER(self.jou_type.inner_type.ctypes_type())
        ptr = ctypes.cast(self.ctypes_value, real_ptr_type)

        # Construct a new object that does not refer back to the pointer implicitly
        result = self.jou_type.inner_type.ctypes_type()()
        ctypes.pointer(result)[0] = ptr[0]
        return JouValue(self.jou_type.inner_type, result)

    def deref_set(self, value: JouValue | int) -> None:
        """Set the value of a pointer."""
        assert self.jou_type.name.endswith("*")

        if isinstance(value, int):
            assert self.jou_type.inner_type is not None
            value = jou_integer(self.jou_type.inner_type, value)

        if self.jou_type.inner_type != value.jou_type:
            raise RuntimeError(f"cannot assign {value} into pointer {self}")

        # We store all pointers to void*, let's undo that
        real_ptr_type = ctypes.POINTER(self.jou_type.inner_type.ctypes_type())
        ptr = ctypes.cast(self.ctypes_value, real_ptr_type)

        ptr[0] = value.ctypes_value

    def unwrap_value(self) -> int:
        """Turn pointer or integer to Python `int`."""
        if self.jou_type.name.endswith("*"):
            return ctypes.cast(self.ctypes_value, ctypes.c_void_p).value or 0
        else:
            # e.g. ctypes.c_int
            return self.ctypes_value.value

    def get_field_pointer(self, field_name: str) -> JouValue:
        """Compute `&ptr.field` where `ptr` is a pointer to an instance of a class."""
        assert self.jou_type.name.endswith("*")
        assert self.jou_type.inner_type is not None

        # We store all pointers to void*, let's undo that
        real_ptr_type = ctypes.POINTER(self.jou_type.inner_type.ctypes_type())
        ptr = ctypes.cast(self.ctypes_value, real_ptr_type)

        # No idea why ctypes makes this so difficult...
        # The problem is that `getattr(instance, field)` returns e.g. Python `int`, not `c_int`
        # https://stackoverflow.com/a/50534262
        klass = self.jou_type.inner_type
        assert klass.class_field_types is not None
        struct = klass.ctypes_type()
        field_type = klass.class_field_types[field_name]
        field_offset = getattr(struct, field_name).offset
        field_ptr = ctypes.pointer(
            field_type.ctypes_type().from_buffer(ptr.contents, field_offset)
        )
        return JouValue(field_type.pointer_type(), field_ptr)

    def pointer_arithmetic(self, index: int) -> JouValue:
        """Compute `&ptr[index]` where `ptr` is a pointer to an instance of a class."""
        if index == 0:
            # Standard library list expects &NULL[0] to be NULL
            return self

        assert self.jou_type.name.endswith("*")
        assert self.jou_type.inner_type is not None
        assert self.jou_type.inner_type.ctypes_type is not None
        assert self.jou_type.ctypes_type is not None

        # We store all pointers to void*, let's undo that
        real_ptr_type = ctypes.POINTER(self.jou_type.inner_type.ctypes_type())
        ptr = ctypes.cast(self.ctypes_value, real_ptr_type)

        memory_address = ctypes.addressof(ptr.contents)
        memory_address += index * ctypes.sizeof(self.jou_type.inner_type.ctypes_type())
        new_ptr = ctypes.cast(memory_address, self.jou_type.ctypes_type())
        return JouValue(self.jou_type, new_ptr)


BASIC_TYPES = {
    "int8": JouType("int8"),
    "int16": JouType("int16"),
    "int32": JouType("int32"),
    "int64": JouType("int64"),
    "uint8": JouType("uint8"),
    "uint16": JouType("uint16"),
    "uint32": JouType("uint32"),
    "uint64": JouType("uint64"),
    "bool": JouType("bool"),
    "float": JouType("float"),
    "double": JouType("double"),
}
BASIC_TYPES["byte"] = BASIC_TYPES["uint8"]
BASIC_TYPES["int"] = BASIC_TYPES["int32"]
BASIC_TYPES["long"] = BASIC_TYPES["int64"]


# argtypes passed as tuple to make the caching work
@functools.cache
def funcptr_type(argtypes: tuple[JouType, ...], return_type: JouType | None) -> JouType:
    argtype_names = [at.name for at in argtypes]
    argtype_names.append("...")  # We assume that absolutely everything takes varargs
    ret_name = "None" if return_type is None else return_type.name
    name = "funcptr(" + ", ".join(argtype_names) + ") -> " + ret_name
    result = JouType(name)
    result.funcptr_argtypes = list(argtypes)
    result.funcptr_return_type = return_type
    return result


def jou_integer(jtype: JouType | str, value: int) -> JouValue:
    if isinstance(jtype, str):
        jtype = BASIC_TYPES[jtype]
    assert jtype.is_integer_type()
    return JouValue(jtype, jtype.ctypes_type()(value))


def jou_bool(value: bool | int) -> JouValue:
    assert value in [False, True, 0, 1]
    return JouValue(BASIC_TYPES["bool"], ctypes.c_uint8(value))


def jou_float(value: float) -> JouValue:
    return JouValue(BASIC_TYPES["float"], ctypes.c_float(value))


def jou_double(value: float) -> JouValue:
    return JouValue(BASIC_TYPES["double"], ctypes.c_double(value))


JOU_VOID_PTR = JouType("void*")
JOU_NULL = JouValue(JOU_VOID_PTR, 0, cast=True)


# None creates a NULL
def jou_string(s: str | bytes | None) -> JouValue:
    if isinstance(s, str):
        s = s.encode("utf-8")
    obj = ctypes.c_char_p(s)
    hey_python_please_dont_garbage_collect_these_strings_thank_you.append(obj)
    return JouValue(BASIC_TYPES["uint8"].pointer_type(), obj, cast=True)


class Parser:
    def __init__(self, path: str, tokens: list[Token]):
        self.path = path
        self.tokens = tokens
        self.in_class_body = False

    # Determines whether the next tokens are "Class{attribute = value}" syntax.
    # Not as simple as you might expect, because the class can be generic.
    # Example: Foo[int, byte*]{x = 1, y="hello"}
    def looks_like_instantiate(self) -> bool:
        if self.tokens[0].kind != "name":
            return False
        if self.tokens[1].code == "{":
            return True
        if self.tokens[1].code == "[":
            # Find matching ']' and see if there's '{' after it.
            depth = 1
            i = 2
            while depth > 0:
                if self.tokens[i].code == "[":
                    depth += 1
                if self.tokens[i].code == "]":
                    depth -= 1
                i += 1
            return self.tokens[i].code == "{"
        return False

    def eat(self, kind_or_code):
        t = self.tokens.pop(0)
        assert t.kind == kind_or_code or t.code == kind_or_code, t
        return t

    def parse_import(self):
        self.eat("import")
        path_spec = self.eat("string").code.strip('"')
        self.eat("newline")

        if path_spec.startswith("."):
            # Relative to directory where the file is
            path = simplify_path(os.path.dirname(self.path) + "/" + path_spec)
        else:
            assert path_spec.startswith("stdlib/")
            path = simplify_path(path_spec)
        return ("import", path)

    def parse_type(self) -> AST:
        t = self.tokens.pop(0)
        assert t.kind == "name" or t.code in (
            "None",
            "void",
            "noreturn",
            "int",
            "long",
            "byte",
            "float",
            "double",
            "bool",
        ), t
        result: Any = ("named_type", t.code)

        while self.tokens[0].code in ("*", "["):
            if self.tokens[0].code == "*":
                result = ("pointer", result)
                self.eat("*")
            else:
                self.eat("[")
                if self.tokens[0].kind.startswith("int") or result[0] != "named_type":
                    # Array, e.g. int[10]
                    #
                    # TODO: This way to distinguish arrays and generics prevents using
                    # constants defined with `const` as array sizes. See:
                    # https://github.com/Akuli/jou/issues/804
                    len_expression = self.parse_expression()
                    self.eat("]")
                    result = ("array", result, len_expression)
                else:
                    # Generic, e.g. List[int]
                    params = []
                    while self.tokens[0].code != "]":
                        params.append(self.parse_type())
                        if self.tokens[0].code != ",":
                            break
                        self.eat(",")
                    self.eat("]")

                    assert result[0] == "named_type"
                    result = ("generic", result[1], params)

        return result

    def parse_name_type_value(self):
        name = self.tokens[0].code
        self.eat("name")
        self.eat(":")
        type = self.parse_type()

        if self.tokens[0].code == "=":
            self.eat("=")
            value = self.parse_expression()
        else:
            value = None

        return (name, type, value)

    def parse_function_or_method_signature(self):
        assert self.tokens[0].kind == "name"
        name = self.tokens[0].code
        self.eat("name")
        self.eat("(")

        args = []
        takes_varargs = False

        while self.tokens[0].code != ")":
            if self.tokens[0].code == "...":
                takes_varargs = True
                self.eat("...")

            elif self.tokens[0].code == "self":
                self.eat("self")
                if self.tokens[0].code == ":":
                    self.eat(":")
                    self_arg: tuple[Any, ...] = ("self", self.parse_type(), None)
                else:
                    self_arg = ("self", None, None)
                args.append(self_arg)

            else:
                args.append(self.parse_name_type_value())

            if self.tokens[0].code != ",":
                break
            self.eat(",")

        self.eat(")")
        self.eat("->")
        return_type = self.parse_type()
        return (name, args, takes_varargs, return_type)

    def parse_call(self, func):
        self.eat("(")
        args = []
        while self.tokens[0].code != ")":
            args.append(self.parse_expression())
            if self.tokens[0].code != ",":
                break
            self.eat(",")
        self.eat(")")
        return ("call", func, args)

    def parse_instantiation(self):
        type = self.parse_type()
        self.eat("{")

        fields = []
        while self.tokens[0].code != "}":
            field_name = self.tokens[0].code
            self.eat("name")
            self.eat("=")
            field_value = self.parse_expression()
            fields.append((field_name, field_value))

            if self.tokens[0].code != ",":
                break
            self.eat(",")

        self.eat("}")
        return ("instantiate", type, fields)

    def parse_array(self):
        self.eat("[")
        result = []
        while self.tokens[0].code != "]":
            result.append(self.parse_expression())
            if self.tokens[0].code != ",":
                break
            self.eat(",")
        self.eat("]")
        assert result
        return ("array", result)

    def parse_elementary_expression(self):
        if self.tokens[0].kind == "int":
            return (
                "integer_constant",
                int(self.tokens.pop(0).code.replace("_", ""), 0),
            )
        if self.tokens[0].kind == "byte":
            code = self.eat("byte").code
            assert code.startswith("'")
            assert code.endswith("'")

            simple_chars = (
                string.ascii_letters
                + string.digits
                + string.punctuation.replace("\\", "").replace("'", "")
                + " "
            )

            if len(code) == 3 and code[1] in simple_chars:
                value = ord(code[1])
            elif code == "'\\0'":
                value = 0
            elif code == "'\\\\'":
                value = ord("\\")
            elif code == "'\\n'":
                value = ord("\n")
            elif code == "'\\r'":
                value = ord("\r")
            elif code == "'\\t'":
                value = ord("\t")
            elif code == "'\\''":
                value = ord("'")
            elif code == "' '":
                value = ord(" ")
            else:
                raise NotImplementedError(code)

            return ("constant", jou_integer("byte", value))
        if self.tokens[0].kind == "double":
            return ("constant", jou_double(float(self.eat("double").code)))
        if self.tokens[0].kind == "string":
            return ("string", unescape_string(self.eat("string").code))
        if self.tokens[0].kind == "name":
            if self.looks_like_instantiate():
                return self.parse_instantiation()
            return ("get_variable", self.eat("name").code)
        # TODO: how far are we gonna get using 0 and 1 bytes as bools?
        if self.tokens[0].code == "True":
            self.eat("True")
            return ("constant", jou_bool(True))
        if self.tokens[0].code == "False":
            self.eat("False")
            return ("constant", jou_bool(False))
        if self.tokens[0].code == "NULL":
            self.eat("NULL")
            return ("constant", JOU_NULL)
        if self.tokens[0].code == "self":
            self.eat("self")
            return ("self",)
        if self.tokens[0].code == "(":
            self.eat("(")
            result = self.parse_expression()
            self.eat(")")
            return result
        if self.tokens[0].code == "[":
            return self.parse_array()
        raise NotImplementedError(self.tokens[0])

    def parse_dot_operator(self, obj):
        self.eat(".")
        name = self.eat("name").code
        return (".", obj, name)

    def parse_indexing(self, obj):
        self.eat("[")
        index = self.parse_expression()
        self.eat("]")
        return ("[", obj, index)

    def parse_expression_with_members_and_indexing_and_calls(self):
        result = self.parse_elementary_expression()
        while True:
            if self.tokens[0].code == "[":
                result = self.parse_indexing(result)
            elif self.tokens[0].code == ".":
                result = self.parse_dot_operator(result)
            elif self.tokens[0].code == "(":
                result = self.parse_call(result)
            else:
                return result

    def parse_expression_with_unary_operators(self):
        prefix = []
        while self.tokens[0].code in {"++", "--", "&", "*", "sizeof"}:
            prefix.append(self.tokens.pop(0))

        result = self.parse_expression_with_members_and_indexing_and_calls()

        suffix = []
        while self.tokens[0].code in {"++", "--"}:
            suffix.append(self.tokens.pop(0))

        while prefix or suffix:
            # ++ and -- "bind tighter", so *foo++ is equivalent to *(foo++)
            # It is implemented by always consuming ++/-- prefixes and suffixes when they exist.
            if prefix and prefix[-1].code == "++":
                kind = "pre_incr"
                del prefix[-1]
            elif prefix and prefix[-1].code == "--":
                kind = "pre_decr"
                del prefix[-1]
            elif suffix and suffix[0].code == "++":
                kind = "post_incr"
                del suffix[0]
            elif suffix and suffix[0].code == "--":
                kind = "post_decr"
                del suffix[0]
            else:
                # We don't have ++ or --, so it must be something in the prefix
                assert prefix and not suffix
                token = prefix.pop()
                if token.code == "*":
                    kind = "dereference"
                elif token.code == "&":
                    kind = "address_of"
                elif token.code == "sizeof":
                    kind = "sizeof"
                else:
                    assert False
            result = (kind, result)

        return result

    def parse_expression_with_mul_and_div(self):
        result = self.parse_expression_with_unary_operators()
        while self.tokens[0].code in {"*", "/", "%"}:
            op = self.tokens.pop(0).code
            rhs = self.parse_expression_with_unary_operators()
            if op == "*":
                result = ("mul", result, rhs)
            elif op == "/":
                result = ("div", result, rhs)
            elif op == "%":
                result = ("mod", result, rhs)
            else:
                raise ValueError("wat?")
        return result

    def parse_expression_with_add(self) -> AST:
        if self.tokens[0].code == "-":
            self.eat("-")
            result: Any = ("negate", self.parse_expression_with_mul_and_div())
        else:
            result = self.parse_expression_with_mul_and_div()

        while self.tokens[0].code in {"+", "-"}:
            op = self.tokens.pop(0).code
            rhs = self.parse_expression_with_mul_and_div()
            if op == "+":
                result = ("add", result, rhs)
            else:
                result = ("sub", result, rhs)

        return result

    def parse_expression_with_bitwise_ops(self):
        result = self.parse_expression_with_add()
        while self.tokens[0].code in {"&", "|", "^"}:
            op = self.tokens.pop(0).code
            rhs = self.parse_expression_with_add()
            if op == "&":
                result = ("bit_and", result, rhs)
            elif op == "|":
                result = ("bit_or", result, rhs)
            elif op == "^":
                result = ("bit_xor", result, rhs)
            else:
                raise ValueError("wat?")
        return result

    # "as" operator has somewhat low precedence, so that "1+2 as float" works as expected
    # TODO: would be better to ban chaining "as" with anything
    def parse_expression_with_as(self):
        result = self.parse_expression_with_bitwise_ops()
        while self.tokens[0].code == "as":
            self.eat("as")
            result = ("as", result, self.parse_type())
        return result

    def parse_expression_with_comparisons(self):
        result = self.parse_expression_with_as()
        while self.tokens[0].code in {"==", "!=", "<", ">", "<=", ">="}:
            op = self.tokens.pop(0).code
            rhs = self.parse_expression_with_as()
            if op == "==":
                result = ("eq", result, rhs)
            elif op == "!=":
                result = ("ne", result, rhs)
            elif op == ">":
                result = ("gt", result, rhs)
            elif op == ">=":
                result = ("ge", result, rhs)
            elif op == "<":
                result = ("lt", result, rhs)
            elif op == "<=":
                result = ("le", result, rhs)
            else:
                raise ValueError("wat?")
        return result

    def parse_expression_with_not(self):
        if self.tokens[0].code == "not":
            self.eat("not")
            result = ("not", self.parse_expression_with_comparisons())
            return result
        else:
            return self.parse_expression_with_comparisons()

    def parse_expression_with_and_or(self):
        result = self.parse_expression_with_not()
        while True:
            if self.tokens[0].code == "and":
                self.eat("and")
                result = ("and", result, self.parse_expression_with_not())
            elif self.tokens[0].code == "or":
                self.eat("or")
                result = ("or", result, self.parse_expression_with_not())
            else:
                return result

    def parse_expression(self):
        return self.parse_expression_with_and_or()

    def parse_oneline_statement(self, decors=()) -> AST:
        location = self.tokens[0].location

        result: AST

        if self.tokens[0].code == "return":
            self.eat("return")
            if self.tokens[0].kind == "newline":
                result = ("return", None)
            else:
                result = ("return", self.parse_expression())
        elif self.tokens[0].code == "assert":
            self.eat("assert")
            cond = self.parse_expression()
            result = ("assert", cond)
        elif self.tokens[0].code in {"pass", "break", "continue"}:
            kw = self.tokens[0].code
            self.eat(kw)
            result = (kw,)
        elif self.tokens[0].code == "const":
            self.eat("const")
            name, type, value = self.parse_name_type_value()
            assert value is not None
            result = ("const", name, type, value, decors)
        elif self.tokens[0].kind == "name" and self.tokens[1].code == ":":
            name, type, value = self.parse_name_type_value()
            # e.g. "foo: int"
            if self.in_class_body:
                assert value is None, self.tokens[0].location
                result = ("class_field", name, type)
            else:
                result = ("declare_local_var", name, type, value)
        else:
            expr = self.parse_expression()
            kind = determine_the_kind_of_a_statement_that_starts_with_an_expression(
                self.tokens[0]
            )
            if kind == "expr_stmt":
                result = ("expr_stmt", expr)
            else:
                self.tokens.pop(0)
                result = (kind, expr, self.parse_expression())

        # TODO: figure out why mypy doesn't like this line
        return result + (location,)  # type: ignore

    def parse_if_statement(self):
        if_and_elifs = []

        self.eat("if")
        while True:
            cond = self.parse_expression()
            body = self.parse_body()
            if_and_elifs.append((cond, body))
            if self.tokens[0].code != "elif":
                break
            self.eat("elif")

        if self.tokens[0].code == "else":
            self.eat("else")
            else_body = self.parse_body()
        else:
            else_body = []

        return ("if", if_and_elifs, else_body)

    def parse_while_loop(self):
        self.eat("while")
        cond = self.parse_expression()
        body = self.parse_body()
        return ("while", cond, body)

    def parse_for_loop(self):
        self.eat("for")

        if self.tokens[0].code == ";":
            init = None
        else:
            init = self.parse_oneline_statement()
        self.eat(";")
        if self.tokens[0].code == ";":
            cond = None
        else:
            cond = self.parse_expression()
        self.eat(";")
        if self.tokens[0].code == ":":
            incr = None
        else:
            incr = self.parse_oneline_statement()

        body = self.parse_body()
        return ("for", init, cond, incr, body)

    def parse_match_statement(self):
        self.eat("match")
        match_obj = self.parse_expression()
        if self.tokens[0].code == "with":
            self.eat("with")
            func = self.parse_expression()
        else:
            func = None

        self.parse_start_of_body()

        cases = []
        case_underscore = []

        while self.tokens[0].kind != "dedent":
            self.eat("case")
            if self.tokens[0].code == "_" and self.tokens[1].code == ":":
                # case _:
                self.eat("_")
                case_underscore = self.parse_body()
            else:
                case_objs = flatten_bitwise_ors(self.parse_expression())
                case_body = self.parse_body()
                cases.append((case_objs, case_body))

        self.eat("dedent")
        return ("match", match_obj, func, cases, case_underscore)

    # Parses the "x: int" part of "x, y, z: int", leaving "y, z: int" to be parsed later.
    def parse_first_of_multiple_local_var_declares(self):
        # Take a backup of the parser where first variable name and its comma are consumed.
        save_state = self.tokens.copy()

        name = self.eat("name").code
        # Skip variables and commas so we can parse the type that comes after it
        while self.tokens[0].code == ",":
            self.eat(",")
            self.eat("name")
        self.eat(":")
        type = self.parse_type()

        self.tokens = save_state
        self.eat("name")
        self.eat(",")

        return ("declare_local_var", name, type, None)

    def parse_statement(self):
        decors = []
        while self.tokens[0].kind == "decorator":
            decors.append(self.eat("decorator").code)
            self.eat("newline")

        location = self.tokens[0].location

        if self.tokens[0].code == "link":
            assert not decors
            self.eat("link")
            self.eat("string")
            self.eat("newline")
            return None

        if self.tokens[0].code == "import":
            assert not decors
            result = self.parse_import()
        elif self.tokens[0].code == "def":
            result = self.parse_function_or_method(decors)
        elif self.tokens[0].code == "declare":
            self.eat("declare")
            if self.tokens[0].code == "global":
                self.eat("global")
                name, type, value = self.parse_name_type_value()
                assert value is None
                self.eat("newline")
                result = ("global_var_declare", name, type, decors)
            else:
                signature = self.parse_function_or_method_signature()
                self.eat("newline")
                result = ("func_declare",) + signature + (decors,)
        elif self.tokens[0].code == "global":
            self.eat("global")
            name, type, value = self.parse_name_type_value()
            assert value is None
            self.eat("newline")
            result = ("global_var_def", name, type, decors)
        elif self.tokens[0].code == "class":
            result = self.parse_class(decors)
        elif self.tokens[0].code == "union":
            assert not decors
            result = self.parse_union()
        elif self.tokens[0].code == "enum":
            result = self.parse_enum(decors)
        elif self.tokens[0].code == "if":
            assert not decors
            result = self.parse_if_statement()
        elif self.tokens[0].code == "for":
            assert not decors
            result = self.parse_for_loop()
        elif self.tokens[0].code == "while":
            assert not decors
            result = self.parse_while_loop()
        elif self.tokens[0].code == "match":
            assert not decors
            result = self.parse_match_statement()
        elif (
            self.tokens[0].kind == "name"
            and self.tokens[1].code == ","
            and self.tokens[2].kind == "name"
        ):
            assert not decors
            result = self.parse_first_of_multiple_local_var_declares()
        else:
            result = self.parse_oneline_statement(decors)
            self.eat("newline")
            return result  # do not add location, it's already added

        return result + (location,)

    def parse_start_of_body(self) -> None:
        self.eat(":")
        self.eat("newline")
        self.eat("indent")

    def parse_body(self):
        self.parse_start_of_body()
        result = []
        while self.tokens[0].kind != "dedent":
            s = self.parse_statement()
            if s is not None:
                result.append(s)
        self.eat("dedent")
        return result

    def parse_function_or_method(self, decors):
        self.eat("def")
        signature = self.parse_function_or_method_signature()

        old = self.in_class_body
        self.in_class_body = False
        body = self.parse_body()
        assert not self.in_class_body
        self.in_class_body = old

        return ("function_def",) + signature + (body, decors)

    def parse_class(self, decors):
        self.eat("class")
        name = self.eat("name").code

        generics = []

        if self.tokens[0].code == "[":
            self.eat("[")
            while self.tokens[0].code != "]":
                generics.append(self.eat("name").code)
                if self.tokens[0].code != ",":
                    break
                self.eat(",")
            self.eat("]")

        assert not self.in_class_body
        self.in_class_body = True
        body = self.parse_body()
        assert self.in_class_body
        self.in_class_body = False

        return ("class", name, generics, body, decors)

    def parse_union(self):
        self.eat("union")
        self.parse_start_of_body()

        result = []
        while self.tokens[0].kind != "dedent":
            field_name, field_type, field_value = self.parse_name_type_value()
            assert field_value is None
            result.append((field_name, field_type))
            self.eat("newline")
        self.eat("dedent")

        assert len(result) >= 2
        return ("union", result)

    def parse_enum(self, decors):
        self.eat("enum")
        name = self.eat("name").code

        self.parse_start_of_body()
        members = []
        while self.tokens[0].kind != "dedent":
            members.append(self.eat("name").code)
            self.eat("newline")
        self.eat("dedent")
        return ("enum", name, members, decors)


def parse_file(path):
    # Remove "foo/../bar" and "foo/./bar" stuff
    path = simplify_path(path)

    if path in ASTS:
        return

    if VERBOSITY >= 2:
        print("Parsing", path)

    with open(path, encoding="utf-8") as f:
        content = f.read()

    tokens = tokenize(content, path)

    parser = Parser(path, tokens)
    ast = []
    imported = []
    while parser.tokens:
        s = parser.parse_statement()
        if s is not None:
            if s[0] == "import":
                imported.append(s[1])
            ast.append(s)

    ASTS[path] = ast

    for imp_path in imported:
        parse_file(imp_path)


def evaluate_compile_time_if_statements() -> None:
    for path, ast in ASTS.items():
        making_progress = True
        while making_progress:
            making_progress = False
            for i, stmt in enumerate(ast):
                if stmt[0] == "if":
                    _, if_and_elifs, else_body, location = stmt
                    cond_ast, then = if_and_elifs.pop(0)
                    if cond_ast[0] == "get_variable":
                        _, cond_varname = cond_ast
                        cond = find_constant(path, cond_varname)
                        assert cond is not None
                    elif cond_ast[0] == "constant":
                        _, cond = cond_ast
                    else:
                        raise RuntimeError(
                            f"cannot evaluate compile-time if statement in {path}"
                        )
                    if cond.unwrap_value():
                        ast[i : i + 1] = then
                    elif not if_and_elifs:
                        ast[i : i + 1] = else_body
                    making_progress = True
                    break


def define_class(path, class_ast, typesub: dict[str, JouType] | None = None):
    assert class_ast[0] == "class", class_ast
    _, class_name, generics, body, decors, location = class_ast

    generic_params = {t: (typesub or {})[t] for t in generics}
    if generic_params:
        cache_key = (class_name, tuple(generic_params.values()))
        full_name = (
            f"{class_name}[{', '.join(t.name for t in generic_params.values())}]"
        )
    else:
        cache_key = class_name
        full_name = class_name

    # While defining this class, it must be possible to refer to it.
    # This is needed for recursive pointer fields.
    jou_type = JouType(full_name)
    jou_type.generic_params = generic_params

    assert cache_key not in TYPES[path], cache_key
    TYPES[path][cache_key] = jou_type

    fields = []
    for member in body:
        if member[0] == "class_field":
            _, field_name, field_type_ast, location = member
            fields.append((field_name, field_type_ast))
        elif member[0] == "union":
            _, union_members, location = member
            # Just treat unions as flat things, good enough
            fields.extend(union_members)
        elif member[0] == "function_def":
            method_name = member[1]
            jou_type.methods[method_name] = (path, member)
        elif member[0] == "pass":
            pass
        else:
            raise NotImplementedError(member[0])

    jou_type.class_field_types = {}
    for field_name, field_type_ast in fields:
        field_type = type_from_ast(path, field_type_ast, typesub=typesub)
        assert field_name not in jou_type.class_field_types
        jou_type.class_field_types[field_name] = field_type

    return jou_type


def find_named_type(path: str, type_name: str):
    try:
        return TYPES[path][type_name]
    except KeyError:
        pass

    result = None

    # Is there a class or enum definition in the same file?
    for item in ASTS[path]:
        if item[:2] == ("class", type_name):
            result = define_class(path, item)
            break
        if item[:2] == ("enum", type_name):
            result = BASIC_TYPES["int"]
            break

    if result is None:
        # Is there a class or enum definition in some imported file?
        for item in ASTS[path]:
            if item[0] == "import":
                _, path2, location = item
                for item2 in ASTS[path2]:
                    if (
                        item2[0] in ("class", "enum")
                        and item2[1] == type_name
                        and "@public" in item2[-2]
                    ):
                        result = find_named_type(path2, type_name)
                        break
                if result is not None:
                    break

    if result is None:
        raise RuntimeError(f"type not found: {type_name}")

    TYPES[path][type_name] = result
    return result


def find_generic_class(path: str, class_name: str, generic_param: JouType):
    try:
        return TYPES[path][class_name, generic_param]
    except KeyError:
        pass

    result = None

    # Is there a class definition in the same file?
    for item in ASTS[path]:
        if item[:2] == ("class", class_name):
            [generic_name] = item[2]  # e.g. "T"
            result = define_class(path, item, typesub={generic_name: generic_param})
            break

    if result is None:
        # Is there a class definition in some imported file?
        for item in ASTS[path]:
            if item[0] == "import":
                _, path2, location = item
                for item2 in ASTS[path2]:
                    if (
                        item2[:2] == ("class", class_name)
                        # location is last, decorators are before that
                        and "@public" in item2[-2]
                    ):
                        result = find_generic_class(path2, class_name, generic_param)
                        break
                if result is not None:
                    break

    if result is None:
        raise RuntimeError(f"generic class not found: {class_name}")

    TYPES[path][class_name, generic_param] = result
    return result


def declare_c_function(path: str, declare_ast) -> JouValue:
    _, func_name, args, takes_varargs, return_type, decors, location = declare_ast
    func = None
    for lib in LIBS:
        try:
            func = getattr(lib, func_name)
            break
        except AttributeError:
            pass

    if func is None:
        raise RuntimeError(f"C function not found: {func_name}")

    for arg_name, arg_type, arg_default in args:
        assert arg_default is None
    argtypes = [type_from_ast(path, triple[1]) for triple in args]
    func.argtypes = [at.ctypes_type() for at in argtypes]

    if return_type == ("named_type", "None") or return_type == (
        "named_type",
        "noreturn",
    ):
        return_type = None
    else:
        return_type = type_from_ast(path, return_type)
    func.restype = None if return_type is None else return_type.ctypes_type()

    return JouValue(funcptr_type(tuple(argtypes), return_type), func)


def declare_c_global_var(path, declare_ast):
    assert declare_ast[0] == "global_var_declare"
    _, varname, vartype_ast, decors, location = declare_ast
    if VERBOSITY >= 1:
        print("Declaring global variable", varname)
    vartype = type_from_ast(path, vartype_ast)
    ctype = vartype.ctypes_type()
    assert ctype is not None

    for lib in LIBS:
        try:
            result = ctype.in_dll(lib, varname)
        except ValueError:
            continue

        return JouValue(vartype.pointer_type(), result)

    raise RuntimeError(f"C global variable not found: {varname}")


# Return values:
#   - AST (tuple) if function defined in Jou
#   - ctypes function if function defined in C and only declared in Jou
#   - None if function not found
def find_function(path: str, func_name: str):
    try:
        return FUNCTIONS[path][func_name]
    except KeyError:
        pass

    # Is it defined or declared in this file?
    result: Any = None
    for ast in ASTS[path]:
        if ast[:2] == ("function_def", func_name):
            result = (path, ast)
            break
        if ast[:2] == ("func_declare", func_name):
            result = declare_c_function(path, ast)
            break

    if result is None:
        # Is it defined or declared in an imported file?
        for item in ASTS[path]:
            if item[0] == "import":
                _, path2, location = item
                for item2 in ASTS[path2]:
                    if (
                        item2[0] in ("function_def", "func_declare")
                        and item2[1] == func_name
                        # location is last, decorators are before that
                        and "@public" in item2[-2]
                    ):
                        result = find_function(path2, func_name)
                        assert result is not None
                        break
                if result is not None:
                    break

    # may assign None, that's fine, next time we know it's not a thing
    FUNCTIONS[path][func_name] = result
    return result


# Returns a pointer to a global variable
def find_global_var_ptr(path, varname):
    try:
        return GLOBALS[path][varname]
    except KeyError:
        pass

    # Is it defined or declared in this file?
    result = None
    for ast in ASTS[path]:
        if ast[:2] == ("global_var_def", varname):
            _, _, var_type, decors, location = ast
            # Create new global variable
            valtype = type_from_ast(path, var_type)
            result = valtype.allocate_instance()
            break
        if ast[:2] == ("global_var_declare", varname):
            result = declare_c_global_var(path, ast)
            break

    if result is None:
        # Is it defined or declared in an imported file?
        for item in ASTS[path]:
            if item[0] == "import":
                _, path2, location = item
                for item2 in ASTS[path2]:
                    if (
                        item2[0] in ("global_var_def", "global_var_declare")
                        and item2[1] == varname
                        # location is last, decorators are before that
                        and "@public" in item2[-2]
                    ):
                        result = find_global_var_ptr(path2, varname)
                        assert result is not None
                        break
                if result is not None:
                    break

    # may assign None, that's fine, next time we know it's not a thing
    GLOBALS[path][varname] = result
    return result


def evaluate_constant(ast: AST, jtype: JouType) -> JouValue:
    if ast[0] == "negate":
        _, inner = ast
        return jou_integer(jtype, evaluate_constant(inner, jtype).unwrap_value())
    if ast[0] == "integer_constant":
        _, int_value = ast
        return jou_integer(jtype, int_value)
    raise NotImplementedError(ast)


def find_constant(path: str, constant_name: str) -> JouValue | None:
    try:
        return CONSTANTS[path][constant_name]
    except KeyError:
        pass

    # Is it defined in this file?
    result = None
    for ast in ASTS[path]:
        if ast[:2] == ("const", constant_name):
            _, _, const_type_ast, value_ast, decors, location = ast
            const_type = type_from_ast(path, const_type_ast)
            result = evaluate_constant(value_ast, const_type)
            assert result.jou_type == const_type
            break
        pass

    if result is None:
        # Is it defined in an imported file?
        for item in ASTS[path]:
            if item[0] == "import":
                _, path2, location = item
                for item2 in ASTS[path2]:
                    # location is last, decorators are before that
                    if item2[:2] == ("const", constant_name) and "@public" in item2[-2]:
                        result = find_constant(path2, constant_name)
                        assert result is not None
                        break
                if result is not None:
                    break

    # may assign None, that's fine, next time we know it's not a thing
    CONSTANTS[path][constant_name] = result
    return result


def find_enum(path, enum_name):
    try:
        return ENUMS[path][enum_name]
    except KeyError:
        pass

    # Is it defined or declared in this file?
    result = None
    for ast in ASTS[path]:
        if ast[:2] == ("enum", enum_name):
            _, _, members, decors, location = ast
            result = members
            break

    if result is None:
        # Is it defined or declared in an imported file?
        for item in ASTS[path]:
            if item[0] == "import":
                _, path2, location = item
                for item2 in ASTS[path2]:
                    if (
                        item2[:2] == ("enum", enum_name)
                        # location is last, decorators are before that
                        and "@public" in item2[-2]
                    ):
                        result = find_enum(path2, enum_name)
                        assert result is not None
                        break
                if result is not None:
                    break

    # may assign None, that's fine, next time we know it's not a thing
    ENUMS[path][enum_name] = result
    return result


def type_from_ast(path, ast, typesub: dict[str, JouType] | None = None) -> JouType:
    if ast[0] == "pointer":
        _, value_type_ast = ast
        if value_type_ast == ("named_type", "void"):
            return JOU_VOID_PTR
        return type_from_ast(path, value_type_ast, typesub=typesub).pointer_type()
    if ast[0] == "named_type":
        _, name = ast
        if typesub is not None and name in typesub:
            return typesub[name]
        return find_named_type(path, name)
    if ast[0] == "array":
        _, item_type_ast, length_ast = ast
        assert length_ast[0] == "integer_constant"
        _, length = length_ast
        return type_from_ast(path, item_type_ast, typesub=typesub).array_type(length)
    if ast[0] == "generic":
        _, class_name, [param_type_ast] = ast
        param_type = type_from_ast(path, param_type_ast, typesub=typesub)
        return find_generic_class(path, class_name, param_type)
    raise NotImplementedError(ast)


class Return(Exception):
    pass


class Break(Exception):
    pass


class Continue(Exception):
    pass


@functools.cache
def get_source_lines(path: str) -> list[str]:
    with open(path, encoding="utf-8") as f:
        return f.readlines()


def cast_array_members_to_a_common_type(array: list[JouValue]) -> list[JouValue]:
    distinct = set(v.jou_type for v in array)
    if len(distinct) == 1:
        return array
    raise NotImplementedError(distinct)


class Runner:
    def __init__(self, path: str) -> None:
        self.path = path
        self.locals: dict[str, JouValue] = {}  # values are pointers

    def run_body(self, body):
        for item in body:
            self.run_statement(item)

    def find_any_var_or_constant(self, varname: str) -> JouValue:
        constant = find_constant(self.path, varname)
        if constant is not None:
            return constant

        if varname in self.locals:
            return self.locals[varname].deref()

        return find_global_var_ptr(self.path, varname)

    def run_statement(self, stmt) -> None:
        filename, lineno = stmt[-1]
        source_line = get_source_lines(filename)[lineno - 1].strip()
        if VERBOSITY >= 2:
            indent = "  " * len(CALL_STACK)
            print(
                f"{indent}Running {stmt[0]!r}: {filename}:{lineno}: {source_line}"
            )

        CALL_STACK.append((filename, lineno))
        try:
            if stmt[0] == "expr_stmt":
                _, expr, location = stmt
                self.run_expression(expr)
            elif stmt[0] == "if":
                _, if_and_elifs, otherwise, location = stmt
                for cond, body in if_and_elifs:
                    if self.run_expression(cond).unwrap_value():
                        self.run_body(body)
                        break
                else:
                    self.run_body(otherwise)
            elif stmt[0] == "assign":
                _, target_ast, value_ast, location = stmt
                if target_ast[0] == "get_variable":
                    _, varname = target_ast
                    if self.find_any_var_or_constant(varname) is None:
                        # Making a new variable. Use the type of the value being assigned.
                        value = self.run_expression(value_ast)
                        ptr = value.jou_type.allocate_instance()
                        ptr.deref_set(value)
                        self.locals[varname] = ptr
                        return
                target = self.run_address_of_expression(target_ast)
                assert target.jou_type.inner_type is not None
                value = self.run_expression(value_ast).cast(target.jou_type.inner_type)
                target.deref_set(value)
            elif stmt[0] == "in_place_add":
                _, target_ast, value_ast, location = stmt
                ptr = self.run_address_of_expression(target_ast)
                value = self.run_expression(value_ast)
                ptr.deref_set(ptr.deref().unwrap_value() + value.unwrap_value())
            elif stmt[0] == "in_place_mul":
                _, target_ast, value_ast, location = stmt
                ptr = self.run_address_of_expression(target_ast)
                value = self.run_expression(value_ast)
                ptr.deref_set(ptr.deref().unwrap_value() * value.unwrap_value())
            elif stmt[0] == "declare_local_var":
                _, varname, type_ast, value_ast, location = stmt
                vartype = type_from_ast(self.path, type_ast)
                var = vartype.allocate_instance()
                if value_ast is not None:
                    var.deref_set(self.run_expression(value_ast).cast(vartype))
                self.locals[varname] = var
            elif stmt[0] == "assert":
                _, cond, location = stmt
                if not self.run_expression(cond):
                    raise RuntimeError(f"assertion failed in {location}")
            elif stmt[0] == "return":
                _, val, location = stmt
                if val is None:
                    # return without a value
                    raise Return(None)
                else:
                    raise Return(self.run_expression(val))
            elif stmt[0] == "for":
                _, init, cond, incr, body, location = stmt
                self.run_statement(init)
                while self.run_expression(cond).unwrap_value():
                    try:
                        self.run_body(body)
                    except Break:
                        break
                    except Continue:
                        pass
                    self.run_statement(incr)
            elif stmt[0] == "while":
                _, cond, body, location = stmt
                while self.run_expression(cond).unwrap_value():
                    try:
                        self.run_body(body)
                    except Break:
                        break
                    except Continue:
                        pass
            elif stmt[0] == "break":
                raise Break()
            elif stmt[0] == "continue":
                raise Continue()
            elif stmt[0] == "match":
                _, match_obj_ast, func_ast, cases, case_underscore, location = stmt
                match_obj = self.run_expression(match_obj_ast)
                func = None if func_ast is None else self.run_expression(func_ast)
                matched = False
                for case_objs, case_body in cases:
                    for case_obj_ast in case_objs:
                        case_obj = self.run_expression(case_obj_ast)
                        if func is None:
                            matched = match_obj.unwrap_value() == case_obj.unwrap_value()
                        else:
                            ret = call_function(func, iter([match_obj, case_obj]))
                            assert ret is not None
                            matched = ret.unwrap_value() == 0
                        if matched:
                            break
                    if matched:
                        self.run_body(case_body)
                        break
                if not matched:
                    self.run_body(case_underscore)
            else:
                raise NotImplementedError(stmt)
        finally:
            p = CALL_STACK.pop()
            assert p == (filename, lineno)

    # Evaluates the type of expr without evaluating expr
    def get_type(self, expr) -> JouType:
        if expr[0] in ("get_variable", "self"):
            return self.run_expression(expr).jou_type

        if expr[0] in ("add", "sub", "mul", "div"):
            _, lhs, rhs = expr
            lhs_type = self.get_type(lhs)
            rhs_type = self.get_type(rhs)
            if lhs_type == rhs_type:
                return lhs_type

            # Given int32 and int64, pick int64.
            if (
                lhs_type.is_integer_type()
                and rhs_type.is_integer_type()
                and lhs_type.name.startswith("int")
                and rhs_type.name.startswith("int")
            ):
                return max([lhs_type, rhs_type], key=(lambda t: int(t.name[3:])))

            raise NotImplementedError(lhs_type, rhs_type)

        if expr[0] == "call":
            # TODO: handle method calls
            _, func_ast, arg_asts = expr
            funcptr_type = self.get_type(func_ast)
            assert funcptr_type.funcptr_return_type is not None
            return funcptr_type.funcptr_return_type

        if expr[0] == "integer_constant":
            # TODO: implicit casts??
            return BASIC_TYPES["int"]

        if expr[0] == "negate":
            _, obj_ast = expr
            inner_type = self.get_type(obj_ast)
            assert inner_type.name.startswith("int")  # not unsigned
            return inner_type

        if expr[0] == "as":
            _, obj_ast, type_ast = expr
            return type_from_ast(self.path, type_ast)

        if expr[0] == ".":
            _, obj_ast, field_name = expr
            # obj.field
            # TODO: what if it's enum or method or whatever
            obj_type = self.get_type(obj_ast)
            if obj_type.class_field_types is None:
                assert obj_type.name.endswith("*")
                assert obj_type.inner_type is not None
                obj_type = obj_type.inner_type
            assert obj_type.class_field_types is not None
            return obj_type.class_field_types[field_name]

        if expr[0] == "[":
            _, ptr_or_array_ast, idx = expr
            ptr_or_array_type = self.get_type(ptr_or_array_ast)
            assert ptr_or_array_type.inner_type is not None
            return ptr_or_array_type.inner_type

        if expr[0] == "sizeof":
            return BASIC_TYPES["int64"]

        if expr[0] == "constant":
            _, value = expr
            return value.jou_type

        raise NotImplementedError(expr)

    # Evaluates &expr
    def run_address_of_expression(self, expr) -> JouValue:
        if expr[0] == "get_variable":
            _, varname = expr
            if varname in self.locals:
                return self.locals[varname]
            var = find_global_var_ptr(self.path, varname)
            if var is None:
                raise RuntimeError(
                    f"no local or global variable named {varname} in {self.path}"
                )
            return var

        elif expr[0] == ".":
            _, obj_ast, field_name = expr

            # To evaluate &pointer.field, we evaluate the pointer and add a
            # memory offset. In this case, we may not be able to evaluate
            # &pointer. For example, &some_function().field is valid if the
            # function returns a pointer, even though &some_function() is not.
            #
            # To get &instance.field we must evaluate &instance and add a
            # memory offset to that. In this case, we may not be able to
            # evaluate the instance itself because it may point to garbage
            # memory.
            if self.get_type(obj_ast).name.endswith("*"):
                ptr = self.run_expression(obj_ast)
            else:
                ptr = self.run_address_of_expression(obj_ast)
            return ptr.get_field_pointer(field_name)

        elif expr[0] == "[":
            # &ptr[index]
            _, ptr_ast, index_ast = expr

            if self.get_type(ptr_ast).is_array_type():
                # &array[index] is slightly different from &ptr[index]
                ptr_to_array = self.run_address_of_expression(ptr_ast)
                assert ptr_to_array.jou_type.inner_type is not None
                assert ptr_to_array.jou_type.inner_type.inner_type is not None
                item_type = ptr_to_array.jou_type.inner_type.inner_type
                ptr = ptr_to_array.cast(item_type.pointer_type())
            else:
                ptr = self.run_expression(ptr_ast)

            index = self.run_expression(index_ast)
            return ptr.pointer_arithmetic(index.unwrap_value())

        elif expr[0] == "dereference":
            # &*foo --> just evaluate foo
            _, obj_ast = expr
            return self.run_expression(obj_ast)

        else:
            raise NotImplementedError(expr)

    def run_expression(self, expr):
        if expr[0] == "call":
            _, func_ast, arg_asts = expr

            if func_ast[0] == ".":
                # It looks like obj.member(). Could be a funcptr call or a
                # method call.
                _, obj_ast, member_name = func_ast
                obj_type = self.get_type(obj_ast)
                class_type = (
                    obj_type.inner_type if obj_type.name.endswith("*") else obj_type
                )
                assert class_type is not None
                if member_name in class_type.methods:
                    # It is a method call
                    method = class_type.methods[member_name]
                    class_def_path, method_def_ast = method

                    self_type_ast = method_def_ast[2][0][1]
                    if self_type_ast is None:
                        self_type = None
                        want_pointer = True
                    else:
                        self_type = type_from_ast(class_def_path, self_type_ast)
                        want_pointer = self_type.name.endswith("*")

                    got_pointer = obj_type.name.endswith("*")

                    if want_pointer and not got_pointer:
                        obj = self.run_address_of_expression(obj_ast)
                    elif got_pointer and not want_pointer:
                        obj = self.run_expression(obj_ast).deref()
                    else:
                        obj = self.run_expression(obj_ast)

                    args_iter: Iterator[JouValue] = itertools.chain(
                        [obj], (self.run_expression(arg_ast) for arg_ast in arg_asts)
                    )
                    return call_function(
                        method,
                        args_iter,
                        self_type=(self_type or class_type.pointer_type()),
                    )

            func = self.run_expression(func_ast)
            args_iter = (self.run_expression(arg_ast) for arg_ast in arg_asts)
            return call_function(func, args_iter)

        elif expr[0] == "get_variable":
            _, varname = expr

            value = self.find_any_var_or_constant(varname)
            if value is not None:
                return value

            func = find_function(self.path, varname)
            if func is not None:
                return func

            raise RuntimeError(f"no variable named {varname} in {self.path}")

        elif expr[0] == "self":
            return self.locals["self"].deref()

        elif expr[0] == "eq":
            left, right = (self.run_expression(ast).unwrap_value() for ast in expr[1:])
            return jou_bool(left == right)

        elif expr[0] == "ne":
            left, right = (self.run_expression(ast).unwrap_value() for ast in expr[1:])
            return jou_bool(left != right)

        elif expr[0] == "gt":
            left, right = (self.run_expression(ast).unwrap_value() for ast in expr[1:])
            return jou_bool(left > right)

        elif expr[0] == "lt":
            left, right = (self.run_expression(ast).unwrap_value() for ast in expr[1:])
            return jou_bool(left < right)

        elif expr[0] == "ge":
            left, right = (self.run_expression(ast).unwrap_value() for ast in expr[1:])
            return jou_bool(left >= right)

        elif expr[0] == "le":
            left, right = (self.run_expression(ast).unwrap_value() for ast in expr[1:])
            return jou_bool(left <= right)

        elif expr[0] == "sizeof":
            _, obj = expr
            t = self.get_type(obj)
            assert t.ctypes_type is not None
            return jou_integer("int64", ctypes.sizeof(t.ctypes_type()))

        elif expr[0] == ".":
            # TODO: can be many other things than plain old field access!
            _, obj_ast, field_name = expr
            if obj_ast[0] == "get_variable":
                _, obj_name = obj_ast
                enum = find_enum(self.path, obj_name)
                if enum is not None:
                    # It is Enum.Member, not instance.field
                    return jou_integer("int", enum.index(field_name))

            obj = self.run_expression(obj_ast)
            if not obj.jou_type.name.endswith("*"):
                # It is an instance of a class, make it a pointer
                ptr = obj.jou_type.allocate_instance()
                ptr.deref_set(obj)
                obj = ptr
            return obj.get_field_pointer(field_name).deref()

        elif expr[0] == "constant":
            _, value = expr
            return value

        elif expr[0] == "integer_constant":
            # TODO: type hints
            _, value = expr
            return jou_integer("int", value)

        elif expr[0] == "address_of":
            _, obj = expr
            return self.run_address_of_expression(obj)

        elif expr[0] == "string":
            _, py_bytes = expr
            return jou_string(py_bytes)

        elif expr[0] == "[":
            _, obj_ast, index_ast = expr
            obj = self.run_expression(obj_ast)
            index = self.run_expression(index_ast)
            if obj.jou_type.is_array_type():
                assert obj.jou_type.inner_type is not None
                # Copy the array to a temporary place where we can index it
                # through a pointer. This way some_function()[1] can be used to
                # ignore a part of the return value.
                ptr = obj.jou_type.allocate_instance()
                ptr.deref_set(obj)
                obj = ptr.cast(obj.jou_type.inner_type.pointer_type())
            return obj.pointer_arithmetic(index.unwrap_value()).deref()

        elif expr[0] == "instantiate":
            _, type_ast, fields = expr
            t = type_from_ast(self.path, type_ast)
            ptr = t.allocate_instance()
            assert t.class_field_types is not None
            for field_name, field_value in fields:
                field_type = t.class_field_types[field_name]
                field_value = self.run_expression(field_value).cast(field_type)
                ptr.get_field_pointer(field_name).deref_set(field_value)
            return ptr.deref()

        elif expr[0] == "as":
            _, obj_ast, type_ast = expr
            obj = self.run_expression(obj_ast)
            t = type_from_ast(self.path, type_ast)
            return obj.cast(t)

        elif expr[0] == "and":
            _, lhs, rhs = expr
            return jou_bool(
                self.run_expression(lhs).unwrap_value()
                and self.run_expression(rhs).unwrap_value()
            )

        elif expr[0] == "or":
            _, lhs, rhs = expr
            return jou_bool(
                self.run_expression(lhs).unwrap_value()
                or self.run_expression(rhs).unwrap_value()
            )

        elif expr[0] == "not":
            _, inner = expr
            return jou_bool(not self.run_expression(inner).unwrap_value())

        elif expr[0] == "add":
            _, lhs_ast, rhs_ast = expr
            lhs = self.run_expression(lhs_ast)
            rhs = self.run_expression(rhs_ast)
            return jou_integer(
                self.get_type(expr), lhs.unwrap_value() + rhs.unwrap_value()
            )

        elif expr[0] == "sub":
            _, lhs_ast, rhs_ast = expr
            lhs = self.run_expression(lhs_ast)
            rhs = self.run_expression(rhs_ast)
            return jou_integer(
                self.get_type(expr), lhs.unwrap_value() - rhs.unwrap_value()
            )

        elif expr[0] == "mul":
            _, lhs_ast, rhs_ast = expr
            lhs = self.run_expression(lhs_ast)
            rhs = self.run_expression(rhs_ast)
            return jou_integer(
                self.get_type(expr), lhs.unwrap_value() * rhs.unwrap_value()
            )

        elif expr[0] == "div":
            _, lhs_ast, rhs_ast = expr
            lhs = self.run_expression(lhs_ast)
            rhs = self.run_expression(rhs_ast)
            # Jou's division is like Python's division: floor (not towards zero)
            return jou_integer(
                self.get_type(expr), lhs.unwrap_value() // rhs.unwrap_value()
            )

        elif expr[0] == "negate":
            _, obj_ast = expr
            obj = self.run_expression(obj_ast)
            return jou_integer(self.get_type(expr), -obj.unwrap_value())

        elif expr[0] in ("pre_incr", "post_incr", "pre_decr", "post_decr"):
            if "incr" in expr[0]:
                diff = 1
            elif "decr" in expr[0]:
                diff = -1
            else:
                raise RuntimeError("wat")

            _, obj_ast = expr
            ptr = self.run_address_of_expression(obj_ast)
            old_value = ptr.deref()
            if old_value.jou_type.name.endswith("*"):
                new_value = old_value.pointer_arithmetic(diff)
            else:
                new_value = jou_integer(
                    old_value.jou_type, old_value.unwrap_value() + diff
                )
            ptr.deref_set(new_value)

            if "pre" in expr[0]:
                return new_value
            if "post" in expr[0]:
                return old_value
            raise RuntimeError("wat")

        elif expr[0] == "array":
            _, item_asts = expr
            items = cast_array_members_to_a_common_type(
                [self.run_expression(item) for item in item_asts]
            )
            item_type = items[0].jou_type
            ctypes_array = (item_type.ctypes_type() * len(items))(
                *(item.ctypes_value for item in items)
            )
            return JouValue(item_type.array_type(len(items)), ctypes_array)

        elif expr[0] == "dereference":
            _, value = expr
            return self.run_expression(value).deref()

        else:
            raise NotImplementedError(expr)


# Args must be given as an iterator to get the right evaluation order AND type
# conversions for arguments. When we evaluate an argument, we need to cast it
# before we evaluate the next argument.
def call_function(
    func: tuple[str, tuple[Any, ...]] | JouValue,
    args_iter,
    *,
    self_type: JouType | None = None,
) -> JouValue | None:
    # Replace T with the actual type in "class Foo[T]"
    if self_type is None:
        typesub = {}
    elif self_type.name.endswith("*"):
        assert self_type.inner_type is not None
        typesub = self_type.inner_type.generic_params
    else:
        typesub = self_type.generic_params

    if isinstance(func, JouValue):
        # Function defined in C and declared in Jou
        args = []
        assert func.jou_type.funcptr_argtypes is not None
        for arg_type in func.jou_type.funcptr_argtypes:
            args.append(next(args_iter).cast(arg_type))
        # Handle varargs: printf("hello %d\n", 1, 2, 3)
        # TODO: use something else than shallow copy for varargs
        args.extend(args_iter)
        result = func.ctypes_value(*(a.ctypes_value for a in args))
        if func.jou_type.funcptr_return_type is None:
            assert result is None
            return None

        if result is None:
            # It is a NULL pointer
            return JOU_NULL.cast(func.jou_type.funcptr_return_type)
        if isinstance(result, int):  # ctypes is funny...
            assert func.jou_type.funcptr_return_type is not None
            result = func.jou_type.funcptr_return_type.ctypes_type()(result)
        return JouValue(func.jou_type.funcptr_return_type, result)

    # Function defined in Jou
    func_path, func_ast = func
    assert func_ast[0] == "function_def"
    (
        _,
        func_name,
        funcdef_args,
        takes_varargs,
        return_type,
        body,
        decors,
        location,
    ) = func_ast
    assert not takes_varargs

    # Do not run the Jou compiler's function for finding standard library,
    # because it looks at the location of currently running executable (python)
    # which is totally unrelated to Jou.
    if func_path == "compiler/paths.jou" and func_name == "find_stdlib":
        return jou_string(os.path.abspath("stdlib"))

    r = Runner(func_path)

    for arg_name, arg_type_ast, arg_default in funcdef_args:
        assert arg_default is None
        if arg_type_ast is None:
            assert arg_name == "self"
            assert self_type is not None
            arg_type = self_type
        else:
            arg_type = type_from_ast(func_path, arg_type_ast, typesub=typesub)
        arg_value = next(args_iter).cast(arg_type)
        # Create a pointer because the argument becomes a local variable that
        # can be mutated.
        ptr = arg_type.allocate_instance()
        ptr.deref_set(arg_value)
        r.locals[arg_name] = ptr

    # iterator must be exhausted now
    try:
        next(args_iter)
    except StopIteration:
        pass
    else:
        raise RuntimeError("too many function arguments")

    try:
        r.run_body(body)
    except Return as r:
        [return_value] = r.args
        return return_value
    else:
        return None


def main() -> None:
    faulthandler.enable()

    LIBS.append(ctypes.CDLL(ctypes.util.find_library("c")))
    LIBS.append(ctypes.CDLL(ctypes.util.find_library("m")))
    LIBS.extend(load_llvm())

    global VERBOSITY
    while len(sys.argv) >= 2 and sys.argv[1].startswith("-"):
        if sys.argv[1] == "--verbose":
            VERBOSITY += 1
            del sys.argv[1]
        elif re.fullmatch(r"-v+", sys.argv[1]):
            VERBOSITY += sys.argv[1].count("v")
            del sys.argv[1]
        else:
            sys.exit(
                f"Usage: python3 {sys.argv[0]} [interpreter args] program.jou [program args]"
            )

    args = (
        sys.argv[1:]
        or "compiler/main.jou -vv -o jou_bootstrap compiler/main.jou".split()
    )

    if VERBOSITY == 1:
        print("Parsing Jou files...", end=" ", flush=True)
    parse_file(args[0])
    if VERBOSITY >= 1:
        print(f"{len(ASTS)} files parsed.")

    for path in ASTS.keys():
        TYPES[path] = BASIC_TYPES.copy()  # type: ignore
        GLOBALS[path] = {}
        FUNCTIONS[path] = {}
        CONSTANTS[path] = {
            "WINDOWS": jou_bool(sys.platform == "win32"),
            "MACOS": jou_bool(sys.platform == "darwin"),
            "NETBSD": jou_bool(sys.platform.startswith("netbsd")),
        }
        ENUMS[path] = {}

    if VERBOSITY >= 1:
        print("Evaluating compile-time if statements...")
    evaluate_compile_time_if_statements()

    argc = jou_integer("int", len(args))

    # Set argv so that it has NULL at end
    ctypes_argv_type = BASIC_TYPES["uint8"].pointer_type().ctypes_type() * (
        len(args) + 1
    )
    ctypes_argv = ctypes_argv_type()
    for i, arg in enumerate(args):
        ctypes_argv[i] = jou_string(arg).ctypes_value
    argv = JouValue(
        BASIC_TYPES["byte"].pointer_type().pointer_type(), ctypes_argv, cast=True
    )

    if VERBOSITY >= 1:
        print("Running main(). Here We Go...")
    main = find_function(args[0], "main")
    call_function(
        func=main,
        args_iter=iter([argc, argv] if main[1][2] else []),
    )


if __name__ == "__main__":
    main()
