"""This file converts Jou code to C code.

This is meant to be used only when setting up Jou on a system for the first
time. Usually you don't need to run this manually, because bootstrap.sh figures
out where Python is installed and invokes this with whatever Python you have.

This is basically a mini Jou compiler. Here are the main ways how this differs
from the full Jou compiler:
- Somewhat horrible error handling, assumes that the code is ok
- Outputs ugly C code instead of an executable
- Does not support all Jou syntax, just needs enough to compile a Jou compiler
- Avoids compiling parts of source files that are not used
- Not tested with ./runtests.sh
- Not meant to be maintained very often
- Coding style is "quick and dirty"
"""

from __future__ import annotations

import itertools
import functools
import sys
import os
import string
import re
from dataclasses import dataclass
from typing import Any, TYPE_CHECKING

if TYPE_CHECKING:
    # First element is always a string, but explainin that to mypy without
    # getting unnecessary errors turned out to be too complicated.
    AST = tuple[Any, ...]


# GLOBAL STATE GO BRRRR
# This place is where all mutated global variables go.
ASTS: dict[str, Any] = {}
FUNCTIONS: dict[str, dict[str, JouValue]] = {}
TYPES: dict[str, dict[str | tuple[str, tuple[JouType, ...] | JouType], JouType]] = {}
GLOBALS: dict[str, dict[str, JouValue | None]] = {}  # None means not found
CONSTANTS: dict[str, dict[str, JouValue | None]] = {}  # None means not found
ENUMS: dict[str, dict[str, list[str] | None]] = {}  # None means not found
GLOBAL_NAME_COUNTER = itertools.count()
FUNCTION_QUEUE: list[tuple[str, AST, JouType | None]] = []


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
        | \b array_count \b
        | \b typedef \b
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

        self.inner_type: JouType | None = None
        self.class_def_path: str | None = None
        self.class_def_public = False
        self.class_field_types: dict[str, JouType] | None = None
        self.method_asts: dict[str, AST] = {}
        self.generic_params: dict[str, JouType] = {}
        self.funcptr_argtypes: list[JouType] | None = None
        self.funcptr_return_type: JouType | None = None

        # These are created lazily
        self._c_name: Any | None = None
        self._only_fwd_declared = False
        self._pointer_type: JouType | None = None
        self._array_types: dict[int, JouType] = {}
        self._method_funcptrs: dict[str, JouValue] = {}

    def __repr__(self) -> str:
        return f"<JouType: {self.name}>"

    def is_integer(self) -> bool:
        # Kinda hacky, but works fine
        return bool(re.fullmatch(r"u?int(8|16|32|64)", self.name))

    def is_array(self) -> bool:
        # More hacky :)
        return bool(re.fullmatch(r".*\[[0-9]+\]", self.name))

    def is_class(self) -> bool:
        return self.class_def_path is not None

    def to_c(self, *, fwd_decl_is_enough: bool = False) -> str:
        if self._c_name is not None and not (
            self._only_fwd_declared and not fwd_decl_is_enough
        ):
            return self._c_name

        result: Any

        if self.name == "bool":
            result = "uint8_t"

        elif self.name == "float":
            result = "float"

        elif self.name == "double":
            result = "double"

        elif self.is_integer():
            result = self.name + "_t"

        elif self.name.startswith("funcptr("):
            # Use a typedef to get sane syntax for these bad boiis
            n = next(GLOBAL_NAME_COUNTER)
            assert self.funcptr_argtypes is not None
            assert self.funcptr_return_type is not None
            args = ", ".join(at.to_c() for at in self.funcptr_argtypes) or "void"
            return_type = self.funcptr_return_type.to_c()
            print(f"// Create typedef for Jou funcptr type {self.name}")
            print(f"typedef {return_type} (*funcptr{n})({args});")
            print()
            result = f"funcptr{n}"

        elif self.name == "void*":
            return "void*"

        elif self.name.endswith("*"):
            # Most of the time it's good to use void* for pointers where you
            # don't care about the contents. This helps in various corner cases
            # where classes refer to each other.
            assert self.inner_type is not None
            return self.inner_type.to_c(fwd_decl_is_enough=True) + "*"

        elif self.is_array():
            # Array. Wrap it in a struct so that they can be passed around on
            # stack freely. For example, you cannot return an array in C, but
            # you can return a struct.
            array_len = int(self.name.split("[")[-1].strip("]"))
            assert self.inner_type is not None
            inner_c = self.inner_type.to_c()
            result = f"array{array_len}_" + re.sub(r"[^A-Za-z0-9_]", "_", inner_c)
            print(f"// Create typedef for Jou array type {self.name}")
            print("typedef struct {", inner_c, "items[", array_len, "]; }", result, ";")
            print()

        elif self.is_class():
            assert self.class_def_path is not None
            assert self.class_field_types is not None
            if self.class_def_public:
                result = "jou_"
            else:
                # It is a private class. Name it so that multiple files can have
                # private classes with the same name.
                i = list(ASTS.keys()).index(self.class_def_path)
                result = f"jou{i}_"

            result += self.name.split("[")[0]

            for gtype in self.generic_params.values():
                result += "_"
                result += re.sub(
                    r"[^A-Za-z0-9_]", "_", gtype.to_c(fwd_decl_is_enough=True)
                )

            if not self._only_fwd_declared:
                print(
                    f"// Forward-declare the struct for {self.name} so that it is accessible everywhere."
                )
                print(f"struct {result};")
                print(f"typedef struct {result} {result};")
                print()

            if not fwd_decl_is_enough:
                fields_c = [
                    ftype.to_c() + " jou_" + fname + ";"
                    for fname, ftype in self.class_field_types.items()
                ]

                print(
                    f"// Fully define the struct for {self.name}. Just forward-declaring is no longer enough."
                )
                print("struct", result, "{")
                print("\n".join("    " + f for f in fields_c) or "    int dummy;")
                print("};")
                print()

            self._only_fwd_declared = fwd_decl_is_enough

        else:
            raise NotImplementedError(self)

        self._c_name = result
        return result

    def get_method(self, name: str) -> JouValue:
        assert not self.name.endswith("*")  # should not be called on pointers

        if name in self._method_funcptrs:
            return self._method_funcptrs[name]

        ast = self.method_asts[name]

        assert self.class_def_path is not None
        result, decl = declare_c_function(self.class_def_path, ast, self)

        print(
            f"// Forward-declare Jou method {self.name}.{name}() so it can be called."
        )
        print(f"// The body of {self.name}.{name}() will be processed later.")
        print(decl + ";")
        print()

        FUNCTION_QUEUE.append((self.class_def_path, ast, self))

        self._method_funcptrs[name] = result
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


@dataclass
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

    type: JouType
    c_code: str


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
    assert jtype.is_integer()

    # The biggest values we support are unsigned long long (ULL).
    # The most negative values we support are long long (LL).
    if value >= 0:
        return JouValue(jtype, f"(({jtype.to_c()})({value}ULL))")
    else:
        return JouValue(jtype, f"(({jtype.to_c()})({value}LL))")


def jou_bool(value: bool | int) -> JouValue:
    assert value in [False, True, 0, 1]
    return JouValue(BASIC_TYPES["bool"], "true" if value else "false")


def jou_float(value: float) -> JouValue:
    return JouValue(BASIC_TYPES["float"], f"((float)({value}))")


def jou_double(value: float) -> JouValue:
    return JouValue(BASIC_TYPES["double"], f"((double)({value}))")


def c_string(s: str | bytes) -> str:
    simple_chars = (
        string.ascii_letters
        + string.digits
        + string.punctuation.replace("\\", "").replace('"', "")
        + " "
    ).encode("ascii")

    result = ""
    for byte in s.encode("utf-8") if isinstance(s, str) else s:
        if byte in simple_chars:
            result += chr(byte)
        elif byte == ord("\n"):
            result += "\\n"
        else:
            # This could be used for everything, but then debugging would be
            # quite painful.
            #
            # We cannot use hex because C consumes as many characters as
            # possible in hex escapes, so "\x22compiler" attempts to interpret
            # \x22c as a byte because c is a hexadecimal digit...
            result += f"\\{byte:03o}"  # e.g. \011 for tab character \t

    return '"' + result + '"'


# None creates a NULL
def jou_string(s: str | bytes | None) -> JouValue:
    if s is None:
        c_code = "((uint8_t*) NULL)"
    else:
        c_code = f"((uint8_t*) {c_string(s)})"

    return JouValue(BASIC_TYPES["uint8"].pointer_type(), c_code)


JOU_VOID_PTR = JouType("void*")
JOU_NULL = JouValue(JOU_VOID_PTR, "NULL")


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
                if self.tokens[0].kind.startswith("int") or result[0] != "named_type" or self.tokens[0].code == "MAX_ARGS":
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
        while self.tokens[0].code in {"++", "--", "&", "*", "sizeof", "array_count"}:
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
                elif token.code == "array_count":
                    kind = "array_count"
                else:
                    assert False
            if kind == "array_count":
                # array_count(x) == sizeof(x)/sizeof(x[0])
                result = ("div", ("sizeof", result), ("sizeof", ("[", result, ("integer_constant", 0))))
            else:
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

    def parse_expression_with_ternary(self):
        result = self.parse_expression_with_and_or()
        if self.tokens[0].code != "if":
            return result

        then = result
        self.eat("if")
        condition = self.parse_expression_with_and_or()
        self.eat("else")
        otherwise = self.parse_expression_with_and_or()
        return ("ternary_if", then, condition, otherwise)

    def parse_expression(self):
        return self.parse_expression_with_ternary()

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
        elif self.tokens[0].code == "typedef":
            self.eat("typedef")
            name = self.eat("name").code
            self.eat("=")
            actual_type = self.parse_type()
            result = ("typedef", name, actual_type, decors)
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
                result = ("function_declare",) + signature + (decors,)
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
    print(f"// Parsing file: {path}")
    # Remove "foo/../bar" and "foo/./bar" stuff
    path = simplify_path(path)
    assert path not in ASTS

    with open(path, encoding="utf-8") as f:
        content = f.read()

    # TODO: hacks to work around lack of function pointers support
    content = content.replace("funcptr(void*) -> void*", "void*")  # declare pthread_create(...)

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
    TYPES[path] = BASIC_TYPES.copy()  # type: ignore
    GLOBALS[path] = {}
    FUNCTIONS[path] = {}
    CONSTANTS[path] = {
        "WINDOWS": jou_bool(sys.platform == "win32"),
        "MACOS": jou_bool(sys.platform == "darwin"),
        "NETBSD": jou_bool(sys.platform.startswith("netbsd")),
    }
    ENUMS[path] = {}


def parse_an_imported_file() -> bool:
    for ast in list(ASTS.values()):
        for stmt in ast:
            if stmt[0] == "import" and stmt[1] not in ASTS:
                parse_file(stmt[1])
                assert stmt[1] in ASTS
                return True
    return False


def evaluate_compile_time_condition(path, ast) -> bool:
    if ast[0] == "get_variable":
        _, cond_varname = ast
        cond_value = find_constant(path, cond_varname)
        assert cond_value is not None
        if cond_value.c_code == "true":
            return True
        if cond_value.c_code == "false":
            return False

    if ast[0] == "constant":
        _, cond_value = ast
        if cond_value.c_code == "true":
            return True
        if cond_value.c_code == "false":
            return False

    if ast[0] == "not":
        _, inner = ast
        return not evaluate_compile_time_condition(path, inner)

    raise RuntimeError(
        f"cannot evaluate compile-time condition in {path}: {ast}"
    )


def evaluate_a_compile_time_if_statement() -> bool:
    for path, ast in ASTS.items():
        for i, stmt in enumerate(ast):
            if stmt[0] != "if":
                continue

            _, if_and_elifs, else_body, location = stmt
            cond_ast, then = if_and_elifs.pop(0)
            cond = evaluate_compile_time_condition(path, cond_ast)
            if cond:
                ast[i : i + 1] = then
            elif not if_and_elifs:
                ast[i : i + 1] = else_body

            print(f"// Evaluated a compile-time if statement in {path}")
            return True

    return False


def get_funcdef_c_name(path: str, ast: AST, klass: JouType | None) -> str:
    if ast[0] == "function_declare":
        # C function names are never changed/mangled
        assert klass is None
        return ast[1]

    assert ast[0] == "function_def"
    jou_name = ast[1]

    if klass is not None:
        # It is a method
        return klass.to_c(fwd_decl_is_enough=True) + "_" + jou_name

    # TODO: handle methods!

    if jou_name == "main":
        return "main"

    if "@public" in ast[-2]:
        return f"jou_{jou_name}"

    # It is a private function. Name it so that multiple files can have
    # private functions with the same name.
    i = list(ASTS.keys()).index(path)
    return f"jou{i}_{jou_name}"


def function_ast_to_value(path: str, ast: AST, klass: JouType | None) -> JouValue:
    if ast[0] == "function_def":
        _, func_name, args, takes_varargs, return_type_ast, body, decors, location = ast
    elif ast[0] == "function_declare":
        _, func_name, args, takes_varargs, return_type_ast, decors, location = ast
    else:
        raise NotImplementedError(ast)

    if klass is None:
        typesub = {}
    else:
        typesub = klass.generic_params

    argtypes = []
    for arg_name, arg_type, arg_default in args:
        assert arg_default is None
        if arg_type is None:
            # TODO: this logic is in two places
            assert arg_name == "self"
            assert klass is not None
            argtypes.append(klass.pointer_type())
        else:
            argtypes.append(type_from_ast(path, arg_type, typesub))

    if return_type_ast in [("named_type", "None"), ("named_type", "noreturn")]:
        return_type = None
    else:
        return_type = type_from_ast(path, return_type_ast, typesub)

    return JouValue(
        funcptr_type(tuple(argtypes), return_type), get_funcdef_c_name(path, ast, klass)
    )


def define_class(
    path: str, class_ast: AST, *, typesub: dict[str, JouType] | None = None
):
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
    jou_type.class_def_path = path
    jou_type.class_def_public = "@public" in class_ast[-2]
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
            jou_type.method_asts[method_name] = member
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


def find_named_type(path: str, type_name: str, *, fwd_decl_is_enough: bool = False):
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
        if item[:2] == ("typedef", type_name):
            result = type_from_ast(path, item[2])
            break

    if result is None:
        # Is there a class or enum definition in some imported file?
        for item in ASTS[path]:
            if item[0] == "import":
                _, path2, location = item
                for item2 in ASTS[path2]:
                    if (
                        item2[0] in ("class", "enum", "typedef")
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


def declare_c_function(
    path: str, ast: AST, klass: JouType | None
) -> tuple[JouValue, str]:
    if ast[0] == "function_def":
        _, func_name, args, takes_varargs, return_type_ast, body, decors, location = ast
    elif ast[0] == "function_declare":
        _, func_name, args, takes_varargs, return_type_ast, decors, location = ast
    else:
        raise RuntimeError(ast[0])

    if klass is not None:
        assert not klass.name.endswith("*")

    func = function_ast_to_value(path, ast, klass)
    assert func.type.funcptr_argtypes is not None

    arg_strings = []
    for (arg_name, _, _), argtype in zip(args, func.type.funcptr_argtypes):
        arg_strings.append(f"{argtype.to_c()} var_{arg_name}")

    if (
        func_name == "main"
        and len(arg_strings) == 2
        and arg_strings[1].startswith("uint8_t**")
    ):
        # clang is picky. It wants "char **argv", not "uint8_t **argv".
        #
        # I don't think there's a good way to silence that error, so let's
        # work around it instead.
        arg_strings[1] = arg_strings[1].replace("uint8_t", "char", 1)

    if takes_varargs:
        arg_strings.append("...")

    if func.type.funcptr_return_type is None:
        ret_str = "void"
    else:
        ret_str = func.type.funcptr_return_type.to_c()

    c_name = get_funcdef_c_name(path, ast, klass)
    declaration = f"{ret_str} {c_name}({', '.join(arg_strings)})"
    return (func, declaration)


def find_function(path: str, func_name: str):
    try:
        return FUNCTIONS[path][func_name]
    except KeyError:
        pass

    # Is it defined or declared in this file?
    result: Any = None
    for ast in ASTS[path]:
        if ast[:2] == ("function_def", func_name):
            result, decl = declare_c_function(path, ast, None)

            print(f"// Forward-declare Jou function {func_name}() so it can be called.")
            print(f"// The body of {func_name}() will be processed later.")
            print(decl + ";")
            print()

            FUNCTION_QUEUE.append((path, ast, None))
            break

        if ast[:2] == ("function_declare", func_name):
            result, decl = declare_c_function(path, ast, None)
            print(
                f"// Declaring function {func_name}() according to Jou declare statement in {path}."
            )
            print(decl + ";")
            print()
            break

    if result is None:
        # Is it defined or declared in an imported file?
        for item in ASTS[path]:
            if item[0] == "import":
                _, path2, location = item
                for item2 in ASTS[path2]:
                    if (
                        item2[0] in ("function_def", "function_declare")
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
            # Create new global variable
            # TODO: numbered naming for non-public globals?
            _, _, vartype_ast, decors, location = ast
            vartype = type_from_ast(path, vartype_ast)
            vartype_c = vartype.to_c()
            print(f"// Create global variable {varname} according to {path}.")
            print(f"{vartype_c} jou_g_{varname};")
            print()
            result = JouValue(vartype.pointer_type(), f"(&jou_g_{varname})")
            break
        if ast[:2] == ("global_var_declare", varname):
            # Declare a C global variable
            _, varname, vartype_ast, decors, location = ast
            vartype = type_from_ast(path, vartype_ast)
            print(f"// Declare global C variable {varname} according to {path}.")
            print(f"extern {vartype.to_c()} {varname};")
            print()
            result = JouValue(vartype.pointer_type(), f"(&{varname})")
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
        inner_string = evaluate_constant(inner, jtype)
        return JouValue(jtype, f"(({jtype.to_c()})(-{inner_string.c_code}))")
    if ast[0] == "integer_constant":
        _, int_value = ast
        return jou_integer(jtype, int_value)
    if ast[0] == "string":
        _, string = ast
        return jou_string(string)
    if ast[0] == "constant":
        _, value = ast
        return value
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
            assert result.type == const_type  # TODO: casts and such
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
        if length_ast[0] == "integer_constant":
            _, length = length_ast
        else:
            assert length_ast[0] == "get_variable", length_ast
            _, length_varname = length_ast
            length_value = find_constant(path, length_varname)
            assert length_value.c_code.startswith('((int32_t)(')
            assert length_value.c_code.endswith('ULL))')
            length = int(length_value.c_code[11:-5])
        return type_from_ast(path, item_type_ast, typesub=typesub).array_type(length)
    if ast[0] == "generic":
        _, class_name, [param_type_ast] = ast
        param_type = type_from_ast(path, param_type_ast, typesub=typesub)
        return find_generic_class(path, class_name, param_type)
    raise NotImplementedError(ast)


@functools.cache
def get_source_lines(path: str) -> list[str]:
    with open(path, encoding="utf-8") as f:
        return f.readlines()


def decide_array_item_type(item_types: list[JouType]) -> JouType:
    distinct_types = set(item_types)
    if len(distinct_types) >= 2 and JOU_VOID_PTR in distinct_types:
        distinct_types.remove(JOU_VOID_PTR)
    if len(distinct_types) == 1:
        return list(distinct_types)[0]
    raise NotImplementedError(item_types)


# As the name suggests, this class creates C functions.
# They correspond to Jou functions and methods.
#
# Most of the output goes to separate list rather than stdout directly,
# because we may notice in the middle of a C function that we still need to
# declare or define something global. The separate list is appended to the
# output later.
class CFuncMaker:
    def __init__(self, path: str, return_type: JouType | None) -> None:
        self.path = path
        self.locals: dict[str, JouType] = {}
        self.output: list[str] = []
        self.name_counter = itertools.count()
        self.loops: list[int] = []
        self.return_type = return_type

    def local_var_ptr(self, name: str) -> JouValue:
        """Return pointer to local variable"""
        return JouValue(self.locals[name].pointer_type(), "(&var_" + name + ")")

    def add_variable(self, jtype: JouType) -> JouValue:
        n = next(self.name_counter)
        zero_init = "{0}" if jtype.is_class() or jtype.is_array() else "0"
        self.output.insert(0, f"{jtype.to_c()} tmp{n} = {zero_init};")

        # Make sure the type is fully defined so we can use it
        t: JouType | None = jtype
        while t is not None:
            t.to_c()
            t = t.inner_type

        return JouValue(jtype, f"tmp{n}")

    def cast(self, value: JouValue, to: JouType) -> JouValue:
        if value.type == to:
            return value

        var = self.add_variable(to)
        if to.is_array():
            # Example:   foo: byte[100] = "hi"
            assert value.type == BASIC_TYPES["uint8"].pointer_type()
            assert to.inner_type == BASIC_TYPES["uint8"]
            self.output.append(f"strcpy((uint8_t*)&{var.c_code}, {value.c_code});")
        else:
            if value.type.is_array():
                self.output.append(f"{var.c_code} = ({to.to_c()}) &{value.c_code};")
            else:
                self.output.append(f"{var.c_code} = ({to.to_c()}) {value.c_code};")
        return var

    def deref(self, ptr: JouValue) -> JouValue:
        assert ptr.type.name.endswith("*")
        assert ptr.type.inner_type is not None
        value_type = ptr.type.inner_type
        var = self.add_variable(value_type)
        self.output.append(f"{var.c_code} = *{ptr.c_code};")
        return var

    # Args are given as ASTs for various reasons:
    #   - evaluation order
    #   - foo(bar) may or may not need to evaluate &bar (array to pointer conversion)
    def call_function(
        self, func: JouValue, args: list[AST | JouValue]
    ) -> JouValue | None:
        actual_args = []
        assert func.type.funcptr_argtypes is not None
        for arg, arg_type in zip(args, func.type.funcptr_argtypes):
            if isinstance(arg, JouValue):
                actual_args.append(self.cast(arg, arg_type))
            else:
                actual_args.append(
                    self.cast(self.do_expression(arg, arg_type), arg_type)
                )

        # Handle varargs: printf("hello %d\n", 1, 2, 3)
        for arg in args[len(func.type.funcptr_argtypes) :]:
            if not isinstance(arg, JouValue):
                if self.guess_type(arg).is_array():
                    arg = self.do_address_of_expression(arg)
                else:
                    arg = self.do_expression(arg, None)
            if arg.type in (
                BASIC_TYPES["int8"],
                BASIC_TYPES["uint8"],
                BASIC_TYPES["int16"],
                BASIC_TYPES["uint16"],
                BASIC_TYPES["bool"],
            ):
                arg = self.cast(arg, BASIC_TYPES["int32"])
            actual_args.append(arg)

        actual_args_string = ", ".join(a.c_code for a in actual_args)

        if func.type.funcptr_return_type is None:
            self.output.append(f"{func.c_code}({actual_args_string});")
            return None

        ret = self.add_variable(func.type.funcptr_return_type)
        self.output.append(f"{ret.c_code} = {func.c_code}({actual_args_string});")
        return ret

    # Returns value of variable
    def find_any_var_or_constant(self, varname: str) -> JouValue | None:
        constant = find_constant(self.path, varname)
        if constant is not None:
            return constant

        if varname in self.locals:
            return JouValue(self.locals[varname], "var_" + varname)

        ptr = find_global_var_ptr(self.path, varname)
        return None if ptr is None else self.deref(ptr)

    # for init; cond; incr:
    #     body
    def loop(
        self, init: AST | None, cond: AST | None, incr: AST | None, body: list[AST]
    ) -> None:
        if init:
            self.do_statement(init)

        self.output.append("while(1) {")

        if cond:
            cond_c = self.do_expression(cond, BASIC_TYPES["bool"]).c_code
            self.output.append(f"if (!{cond_c}) break;")

        id = next(self.name_counter)
        self.loops.append(id)
        self.do_body(body)
        self.loops.pop()

        # The `continue` keyword must run incr but the first iteration doesn't.
        # Also, unlike in C, the "incr" part may refer things defined in body.
        #
        # DO NOT USE the C continue keyword. It is not compatible with this.
        self.output.append(f"loop{id}_continue: (void)0;")
        if incr:
            self.do_statement(incr)

        self.output.append("}")

    def do_body(self, body):
        for item in body:
            self.do_statement(item)

    def do_statement(self, stmt) -> None:
        filename, lineno = stmt[-1]
        self.output.append(f"// File {filename}, line {lineno}: {stmt[0]}")

        if stmt[0] == "expr_stmt":
            _, expr, location = stmt
            self.do_expression(expr, None)

        elif stmt[0] == "if":
            _, if_and_elifs, otherwise, location = stmt
            end_braces = ""
            for cond, body in if_and_elifs:
                cond = self.do_expression(cond, BASIC_TYPES["bool"])
                if cond.c_code == "true":
                    # Evaluate it and don't bother with the rest.
                    self.do_body(body)
                    otherwise = []
                elif cond.c_code == "false":
                    # Skip this entirely
                    pass
                else:
                    # Fully evaluate it. Nest the rest in the else.
                    self.output.append("if (" + cond.c_code + ") {")
                    self.do_body(body)
                    self.output.append("} else {")
                    end_braces += "}"
            self.do_body(otherwise)
            self.output.extend(end_braces)

        elif stmt[0] == "assign":
            _, target_ast, value_ast, location = stmt
            if target_ast[0] == "get_variable":
                _, varname = target_ast
                if self.find_any_var_or_constant(varname) is None:
                    # Making a new variable. Use the type of the value being assigned.
                    value = self.do_expression(value_ast, None)
                    self.output.insert(0, f"{value.type.to_c()} var_{varname};")
                    self.output.append(f"var_{varname} = {value.c_code};")
                    self.locals[varname] = value.type
                    return
            ptr = self.do_address_of_expression(target_ast)
            assert ptr.type.inner_type is not None
            value = self.cast(
                self.do_expression(value_ast, ptr.type.inner_type), ptr.type.inner_type
            )
            self.output.append(f"*{ptr.c_code} = {value.c_code};")

        elif stmt[0].startswith("in_place_"):
            _, target_ast, value_ast, location = stmt
            ptr = self.do_address_of_expression(target_ast)
            value = self.do_expression(value_ast, ptr.type.inner_type)

            if stmt[0] == "in_place_add":
                self.output.append(f"*{ptr.c_code} += {value.c_code};")
            elif stmt[0] == "in_place_sub":
                self.output.append(f"*{ptr.c_code} -= {value.c_code};")
            elif stmt[0] == "in_place_mul":
                self.output.append(f"*{ptr.c_code} *= {value.c_code};")
            elif stmt[0] == "in_place_div":
                self.output.append(f"*{ptr.c_code} /= {value.c_code};")
            elif stmt[0] == "in_place_bit_and":
                self.output.append(f"*{ptr.c_code} &= {value.c_code};")
            elif stmt[0] == "in_place_bit_or":
                self.output.append(f"*{ptr.c_code} |= {value.c_code};")
            elif stmt[0] == "in_place_bit_xor":
                self.output.append(f"*{ptr.c_code} ^= {value.c_code};")
            else:
                raise NotImplementedError(stmt[0])

        elif stmt[0] == "declare_local_var":
            _, varname, type_ast, value_ast, location = stmt
            vartype = type_from_ast(self.path, type_ast)
            if varname in self.locals:
                # This happens when we do "case X | Y:" and the case body is
                # transpiled twice. If it contains a "foo: int" or similar, we
                # attempt to create two variables named foo. Let's not do that.
                assert self.locals[varname] == vartype
            else:
                self.output.insert(0, f"{vartype.to_c()} var_{varname};")
                self.locals[varname] = vartype
            if value_ast is not None:
                value = self.cast(self.do_expression(value_ast, vartype), vartype)
                self.output.append(f"var_{varname} = {value.c_code};")

        elif stmt[0] == "assert":
            _, cond_ast, (filename, lineno) = stmt
            cond = self.do_expression(cond_ast, BASIC_TYPES["bool"])
            self.output.append("if (!" + cond.c_code + ") {")
            msg = f'Assertion failed in file "{filename}", line {lineno}.'
            self.output.append(
                f"int32_t puts(uint8_t*); puts((uint8_t*) {c_string(msg)});"
            )
            self.output.append("void abort(void); abort();")
            self.output.append("}")

        elif stmt[0] == "return":
            _, val_ast, location = stmt
            if val_ast is None:
                self.output.append("return;")
            else:
                assert self.return_type is not None
                val = self.cast(
                    self.do_expression(val_ast, self.return_type), self.return_type
                )
                self.output.append("return " + val.c_code + ";")

        elif stmt[0] == "while":
            _, cond, body, location = stmt
            self.loop(None, cond, None, body)

        elif stmt[0] == "for":
            _, init, cond, incr, body, location = stmt
            self.loop(init, cond, incr, body)

        elif stmt[0] == "break":
            self.output.append("break;")

        elif stmt[0] == "continue":
            self.output.append(f"goto loop{self.loops[-1]}_continue;")

        elif stmt[0] == "pass":
            pass

        elif stmt[0] == "match":
            _, match_obj_ast, func_ast, cases, case_underscore, location = stmt
            match_obj = self.do_expression(match_obj_ast, None)
            func = None if func_ast is None else self.do_expression(func_ast, None)
            brace_count = 0
            for case_objs, case_body in cases:
                for case_obj_ast in case_objs:
                    case_obj = self.do_expression(case_obj_ast, match_obj.type)
                    if func is None:
                        cond = f"{match_obj.c_code} == {case_obj.c_code}"
                    else:
                        ret = self.call_function(func, [match_obj, case_obj])
                        assert ret is not None
                        cond = f"{ret.c_code} == 0"
                    self.output.append("if (" + cond + ") {")
                    self.do_body(case_body)
                    self.output.append("} else {")
                    brace_count += 1
            self.do_body(case_underscore)
            self.output.extend("}" * brace_count)

        else:
            raise NotImplementedError(stmt)

    # Evaluates the type of expr without the side effects of evaluating expr.
    def guess_type(self, expr: AST) -> JouType:
        if expr[0] in ("get_variable", "self", "constant"):
            return self.do_expression(expr, None).type

        if expr[0] in ("eq", "ne", "gt", "ge", "lt", "le", "and", "or", "not"):
            return BASIC_TYPES["bool"]

        if expr[0] in ("add", "sub", "mul", "div", "mod"):
            _, lhs, rhs = expr
            lhs_type = self.guess_type(lhs)
            rhs_type = self.guess_type(rhs)
            if lhs_type == rhs_type:
                return lhs_type

            if lhs_type.is_integer() and rhs_type.is_integer():
                lhs_signed = lhs_type.name.startswith("int")
                rhs_signed = rhs_type.name.startswith("int")
                lhs_unsigned = lhs_type.name.startswith("uint")
                rhs_unsigned = rhs_type.name.startswith("uint")
                assert lhs_signed or lhs_unsigned
                assert rhs_signed or rhs_unsigned

                # "uint32" --> 32
                # "int32" --> 32
                lhs_bits = int(lhs_type.name.removeprefix("u").removeprefix("int"))
                rhs_bits = int(rhs_type.name.removeprefix("u").removeprefix("int"))

                # For same signed-ness, pick bigger type.
                # Given int32 and int64, pick int64.
                # Given uint32 and uint64, pick uint64.
                if (lhs_signed and rhs_signed) or (lhs_unsigned and rhs_unsigned):
                    return lhs_type if lhs_bits > rhs_bits else rhs_type

                # For signed and unsigned, pick bigger size and signed.
                # Given int32 and uint8, pick int32.
                # Given uint32 and int8, pick int32.
                return BASIC_TYPES["int" + str(max(lhs_bits, lhs_bits))]

            raise NotImplementedError(expr[0], lhs_type, rhs_type)

        if expr[0] == "call":
            _, func_ast, arg_asts = expr

            if func_ast[0] == ".":
                _, obj, member = func_ast
                obj_type = self.guess_type(obj)

                if obj_type.name.endswith("*"):
                    assert obj_type.inner_type is not None
                    obj_type = obj_type.inner_type

                if member in obj_type.method_asts:
                    # It is a method call
                    funcptr = obj_type.get_method(member)
                    assert funcptr.type.funcptr_return_type is not None
                    return funcptr.type.funcptr_return_type

            funcptr_type = self.guess_type(func_ast)
            assert funcptr_type.funcptr_return_type is not None
            return funcptr_type.funcptr_return_type

        if expr[0] == "integer_constant":
            # TODO: implicit casts??
            return BASIC_TYPES["int"]

        if expr[0] == "negate":
            _, obj_ast = expr
            inner_type = self.guess_type(obj_ast)
            assert inner_type.name.startswith("int")  # not unsigned
            return inner_type

        if expr[0] == "as":
            _, obj_ast, type_ast = expr
            return type_from_ast(self.path, type_ast)

        if expr[0] == ".":
            _, obj_ast, field_name = expr

            if obj_ast[0] == "get_variable":
                _, name = obj_ast
                if find_enum(self.path, name) is not None:
                    # Enum.member
                    return BASIC_TYPES["int"]

            obj_type = self.guess_type(obj_ast)
            if obj_type.class_field_types is None:
                assert obj_type.name.endswith("*")
                assert obj_type.inner_type is not None
                obj_type = obj_type.inner_type
            assert obj_type.class_field_types is not None
            return obj_type.class_field_types[field_name]

        if expr[0] == "[":
            _, ptr_or_array_ast, idx = expr
            ptr_or_array_type = self.guess_type(ptr_or_array_ast)
            assert ptr_or_array_type.inner_type is not None
            return ptr_or_array_type.inner_type

        if expr[0] == "sizeof":
            return BASIC_TYPES["int64"]

        if expr[0] == "address_of":
            _, value = expr
            return self.guess_type(value).pointer_type()

        if expr[0] == "dereference":
            _, ptr = expr
            ptr_type = self.guess_type(ptr)
            assert ptr_type.name.endswith("*")
            assert ptr_type.inner_type is not None
            return ptr_type.inner_type

        if expr[0] == "string":
            return BASIC_TYPES["byte"].pointer_type()

        if expr[0] == "instantiate":
            return type_from_ast(self.path, expr[1])

        if expr[0] in ("pre_incr", "pre_decr", "post_incr", "post_decr"):
            _, obj = expr
            return self.guess_type(obj)

        if expr[0] == "array":
            _, items = expr
            itype = decide_array_item_type([self.guess_type(item) for item in items])
            return itype.array_type(len(items))

        if expr[0] == "ternary_if":
            _, then, condition, otherwise = expr
            return self.guess_type(then)  # good enough??

        raise NotImplementedError(expr)

    # Evaluates &expr
    def do_address_of_expression(self, expr) -> JouValue:
        if expr[0] == "get_variable":
            _, varname = expr
            if varname in self.locals:
                return self.local_var_ptr(varname)
            var = find_global_var_ptr(self.path, varname)
            if var is None:
                raise RuntimeError(
                    f"no local or global variable named {varname} in {self.path}"
                )
            return var

        elif expr[0] == "self":
            return self.local_var_ptr("self")

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
            #
            # TODO: is guess_type() good enough?
            if self.guess_type(obj_ast).name.endswith("*"):
                ptr = self.do_expression(obj_ast, None)
            else:
                ptr = self.do_address_of_expression(obj_ast)
            result = self.add_variable(self.guess_type(expr).pointer_type())
            self.output.append(f"{result.c_code} = &{ptr.c_code}->jou_{field_name};")
            return result

        elif expr[0] == "[":
            # &ptr[index]
            _, ptr_ast, index_ast = expr

            if self.guess_type(ptr_ast).is_array():
                # &array[index] is slightly different from &ptr[index]
                ptr_to_array = self.do_address_of_expression(ptr_ast)
                assert ptr_to_array.type.inner_type is not None
                assert ptr_to_array.type.inner_type.inner_type is not None, ptr_to_array
                item_type = ptr_to_array.type.inner_type.inner_type
                ptr = self.cast(ptr_to_array, item_type.pointer_type())
            else:
                ptr = self.do_expression(ptr_ast, None)

            index = self.do_expression(index_ast, BASIC_TYPES["int64"])
            result = self.add_variable(ptr.type)
            self.output.append(f"{result.c_code} = {ptr.c_code} + {index.c_code};")
            return result

        elif expr[0] == "dereference":
            # &*foo --> just evaluate foo
            # TODO: pass in the expected type?
            _, obj_ast = expr
            return self.do_expression(obj_ast, None)

        else:
            raise NotImplementedError(expr)

    def do_expression(self, expr, expected_type: JouType | None) -> JouValue:
        if expected_type is not None:
            naive_type = self.guess_type(expr)
            if naive_type.is_array() and expected_type.name.endswith("*"):
                # Implicit array to pointer cast
                return self.cast(self.do_address_of_expression(expr), expected_type)

        if expr[0] == "call":
            _, func_ast, arg_asts = expr

            if func_ast[0] == ".":
                # It looks like obj.member(). Could be a funcptr call or a
                # method call.
                _, obj_ast, member_name = func_ast
                obj_type = self.guess_type(obj_ast)
                class_type = (
                    obj_type.inner_type if obj_type.name.endswith("*") else obj_type
                )
                assert class_type is not None
                if member_name in class_type.method_asts:
                    # It is a method call
                    method = class_type.get_method(member_name)

                    assert method.type.funcptr_argtypes is not None
                    self_type = method.type.funcptr_argtypes[0]

                    want_pointer = self_type.name.endswith("*")
                    got_pointer = obj_type.name.endswith("*")

                    if want_pointer and not got_pointer:
                        obj = self.do_address_of_expression(obj_ast)
                    elif got_pointer and not want_pointer:
                        obj = self.deref(self.do_expression(obj_ast, None))
                    else:
                        obj = self.do_expression(obj_ast, None)

                    return self.call_function(method, [obj] + arg_asts)  # type: ignore

            func = self.do_expression(func_ast, None)
            return self.call_function(func, arg_asts)  # type: ignore

        if expr[0] == "get_variable":
            _, varname = expr

            value = self.find_any_var_or_constant(varname)
            if value is not None:
                return value

            func = find_function(self.path, varname)
            if func is not None:
                return func

            raise RuntimeError(f"no variable named {varname} in {self.path}")

        if expr[0] == "self":
            return JouValue(self.locals["self"], "var_self")

        if expr[0] in ("eq", "ne", "gt", "lt", "ge", "le"):
            _, lhs_ast, rhs_ast = expr
            lhs = self.do_expression(lhs_ast, None)
            rhs = self.do_expression(rhs_ast, None)
            result = self.add_variable(BASIC_TYPES["bool"])
            if expr[0] == "lt":
                self.output.append(f"{result.c_code} = {lhs.c_code} < {rhs.c_code};")
            elif expr[0] == "gt":
                self.output.append(f"{result.c_code} = {lhs.c_code} > {rhs.c_code};")
            elif expr[0] == "le":
                self.output.append(f"{result.c_code} = {lhs.c_code} <= {rhs.c_code};")
            elif expr[0] == "ge":
                self.output.append(f"{result.c_code} = {lhs.c_code} >= {rhs.c_code};")
            elif expr[0] == "eq":
                self.output.append(f"{result.c_code} = {lhs.c_code} == {rhs.c_code};")
            elif expr[0] == "ne":
                self.output.append(f"{result.c_code} = {lhs.c_code} != {rhs.c_code};")
            else:
                raise RuntimeError("wat")
            return result

        elif expr[0] == "sizeof":
            _, obj = expr
            type_c = self.guess_type(obj).to_c(fwd_decl_is_enough=False)
            return JouValue(BASIC_TYPES["int64"], f"((int64_t) sizeof({type_c}))")

        elif expr[0] == ".":
            _, obj_ast, field_name = expr
            if obj_ast[0] == "get_variable":
                _, obj_name = obj_ast
                enum = find_enum(self.path, obj_name)
                if enum is not None:
                    # It is Enum.Member, not instance.field
                    return jou_integer("int", enum.index(field_name))

            obj = self.do_expression(obj_ast, None)
            if obj.type.name.endswith("*"):
                class_type = obj.type.inner_type
                op = "->"
            else:
                class_type = obj.type
                op = "."

            assert class_type is not None
            assert class_type.class_field_types is not None
            ftype = class_type.class_field_types[field_name]
            result = self.add_variable(ftype)
            self.output.append(f"{result.c_code} = {obj.c_code}{op}jou_{field_name};")
            return result

        elif expr[0] == "constant":
            _, value = expr
            return value

        elif expr[0] == "integer_constant":
            # TODO: type hints
            _, value = expr
            return jou_integer("int", value)

        elif expr[0] == "address_of":
            _, obj = expr
            return self.do_address_of_expression(obj)

        elif expr[0] == "string":
            _, py_bytes = expr
            return jou_string(py_bytes)

        elif expr[0] == "[":
            _, obj_ast, index_ast = expr
            obj = self.do_expression(obj_ast, None)
            index = self.do_expression(index_ast, BASIC_TYPES["int64"])
            assert obj.type.inner_type is not None
            result = self.add_variable(obj.type.inner_type)
            if obj.type.is_array():
                self.output.append(
                    f"{result.c_code} = {obj.c_code}.items[{index.c_code}];"
                )
            else:
                self.output.append(f"{result.c_code} = {obj.c_code}[{index.c_code}];")
            return result

        elif expr[0] == "instantiate":
            _, type_ast, fields = expr
            t = type_from_ast(self.path, type_ast)
            result = self.add_variable(t)
            assert t.class_field_types is not None
            for field_name, field_value in fields:
                field_type = t.class_field_types[field_name]
                field_value = self.cast(
                    self.do_expression(field_value, field_type), field_type
                )
                self.output.append(
                    f"{result.c_code}.jou_{field_name} = {field_value.c_code};"
                )
            return result

        elif expr[0] == "as":
            _, obj_ast, type_ast = expr
            t = type_from_ast(self.path, type_ast)
            return self.cast(self.do_expression(obj_ast, t), t)

        elif expr[0] == "and":
            _, lhs_ast, rhs_ast = expr
            result = self.add_variable(BASIC_TYPES["bool"])
            lhs = self.do_expression(lhs_ast, BASIC_TYPES["bool"])
            self.output.append("// and")
            self.output.append(f"if ({lhs.c_code})" + " {")
            rhs = self.do_expression(rhs_ast, BASIC_TYPES["bool"])
            self.output.append(f"{result.c_code} = {rhs.c_code};")
            self.output.append("} else { " + result.c_code + " = false; }")
            return result

        elif expr[0] == "or":
            _, lhs_ast, rhs_ast = expr
            result = self.add_variable(BASIC_TYPES["bool"])
            lhs = self.do_expression(lhs_ast, BASIC_TYPES["bool"])
            self.output.append("// or")
            self.output.append("if(%s) {%s=true;} else {" % (lhs.c_code, result.c_code))
            rhs = self.do_expression(rhs_ast, BASIC_TYPES["bool"])
            self.output.append(f"{result.c_code} = {rhs.c_code};")
            self.output.append("}")
            return result

        elif expr[0] == "not":
            _, obj_ast = expr
            obj = self.do_expression(obj_ast, BASIC_TYPES["bool"])
            result = self.add_variable(BASIC_TYPES["bool"])
            self.output.append(f"{result.c_code} = !{obj.c_code};")
            return result

        elif expr[0] in ("add", "sub", "mul", "div", "mod"):
            _, lhs_ast, rhs_ast = expr
            lhs = self.do_expression(lhs_ast, None)
            rhs = self.do_expression(rhs_ast, None)
            result = self.add_variable(self.guess_type(expr))

            if expr[0] == "add":
                self.output.append(f"{result.c_code} = {lhs.c_code} + {rhs.c_code};")
            elif expr[0] == "sub":
                self.output.append(f"{result.c_code} = {lhs.c_code} - {rhs.c_code};")
            elif expr[0] == "mul":
                self.output.append(f"{result.c_code} = {lhs.c_code} * {rhs.c_code};")
            elif expr[0] == "div":
                # TODO: Jou's integer division rules are different than C's
                self.output.append(f"{result.c_code} = {lhs.c_code} / {rhs.c_code};")
            elif expr[0] == "mod":
                # TODO: Jou's integer modulo rules are different than C's
                self.output.append(f"{result.c_code} = {lhs.c_code} % {rhs.c_code};")
            else:
                raise RuntimeError("wat")

            return result

        elif expr[0] == "negate":
            _, obj_ast = expr
            obj = self.do_expression(obj_ast, None)
            result = self.add_variable(self.guess_type(expr))
            self.output.append(f"{result.c_code} = -{obj.c_code};")
            return result

        elif expr[0] in ("pre_incr", "post_incr", "pre_decr", "post_decr"):
            if "incr" in expr[0]:
                diff = 1
            elif "decr" in expr[0]:
                diff = -1
            else:
                raise RuntimeError("wat")

            _, obj_ast = expr
            ptr = self.do_address_of_expression(obj_ast)
            old_value = self.deref(ptr)
            new_value = self.add_variable(old_value.type)
            self.output.append(f"{new_value.c_code} = {old_value.c_code} + ({diff});")
            self.output.append(f"*{ptr.c_code} = {new_value.c_code};")
            if "pre" in expr[0]:
                return new_value
            if "post" in expr[0]:
                return old_value
            raise RuntimeError("wat")

        elif expr[0] == "array":
            _, item_asts = expr
            itype = decide_array_item_type(
                [self.guess_type(item) for item in item_asts]
            )
            items = [
                self.cast(self.do_expression(item, itype), itype) for item in item_asts
            ]
            var = self.add_variable(itype.array_type(len(items)))
            for i, item in enumerate(items):
                self.output.append(f"{var.c_code}.items[{i}] = {item.c_code};")
            return var

        elif expr[0] == "dereference":
            _, value_ast = expr
            ptype = None if expected_type is None else expected_type.pointer_type()
            value = self.do_expression(value_ast, ptype)
            # TODO: handle *array? or intentionally not?...
            assert value.type.inner_type is not None
            result = self.add_variable(value.type.inner_type)
            self.output.append(f"{result.c_code} = *{value.c_code};")
            return result

        elif expr[0] == "ternary_if":
            _, then, condition, otherwise = expr
            result = self.add_variable(self.guess_type(expr))

            condition_value = self.do_expression(condition, BASIC_TYPES["bool"])
            self.output.append(f"if ({condition_value.c_code}) {{")
            then_value = self.do_expression(then, result.type)
            self.output.append(f"{result.c_code} = {then_value.c_code};")
            self.output.append("} else {")
            otherwise_value = self.do_expression(otherwise, result.type)
            self.output.append(f"{result.c_code} = {otherwise_value.c_code};")
            self.output.append("}")
            return result

        else:
            raise NotImplementedError(expr)


def define_function(path: str, ast: AST, klass: JouType | None) -> None:
    assert ast[0] == "function_def", ast
    _, name, args, takes_varargs, return_type_ast, body, decors, location = ast

    if return_type_ast in [("named_type", "None"), ("named_type", "noreturn")]:
        return_type = None
    else:
        return_type = type_from_ast(
            path, return_type_ast, (None if klass is None else klass.generic_params)
        )
    func_maker = CFuncMaker(path, return_type)

    for arg_name, arg_type_ast, arg_value in args:
        if arg_type_ast is None:
            # TODO: this logic is in two places
            assert arg_name == "self"
            assert klass is not None
            func_maker.locals[arg_name] = klass.pointer_type()
        else:
            func_maker.locals[arg_name] = type_from_ast(
                path, arg_type_ast, (None if klass is None else klass.generic_params)
            )

    try:
        func_maker.do_body(body)
    except Exception as e:
        error: Exception | None = e
    else:
        error = None

    # The function maker has now printed everything it needs to exist globally.
    # We can now print the function we made.

    print(f"// This is the body of function {name}() defined in {path}.")
    declaration = declare_c_function(path, ast, klass=klass)[1]
    print(declaration, "{")

    # Turn this:
    #
    #   } else {
    #   }
    #
    # into this:
    #
    #   }
    #
    i = 0
    while i + 1 < len(func_maker.output):
        if func_maker.output[i] == "} else {" and func_maker.output[i + 1] == "}":
            del func_maker.output[i]
        else:
            i += 1

    indent_level = 4
    for line in func_maker.output:
        if line.split("//")[0].strip().startswith("}"):
            indent_level = max(indent_level - 1, 4)
        print(" " * indent_level + line)
        if line.split("//")[0].strip().endswith("{"):
            indent_level += 1

    # If an error happens, print everything we can. Leave out '}' to show
    # that it is not complete.
    if error:
        raise error

    print("}")
    print()


def main() -> None:
    [main_file] = sys.argv[1:]
    parse_file(main_file)
    while parse_an_imported_file() or evaluate_a_compile_time_if_statement():
        pass

    print()

    print("// Hard-coded dependencies for core functionality.")
    print("#include <stddef.h>")
    print("#include <stdint.h>")
    print("#include <stdbool.h>")
    print("#include <stdnoreturn.h>")
    print()

    # Everything else is done on-demand / as needed.
    main_ast = next(
        item for item in ASTS[main_file] if item[:2] == ("function_def", "main")
    )
    FUNCTION_QUEUE.append((main_file, main_ast, None))

    processed = []
    while FUNCTION_QUEUE:
        triplet = FUNCTION_QUEUE.pop()
        if triplet not in processed:
            processed.append(triplet)
            define_function(*triplet)


if __name__ == "__main__":
    main()
