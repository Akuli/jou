from __future__ import annotations

import subprocess
import shutil
import sys
import os
import string
import ctypes.util
import re
from dataclasses import dataclass
from typing import Any


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
        print("Found LLVM:", path)
        result.append(ctypes.CDLL(path))

    return result


LIBS: list[ctypes.CDLL] = []

KEYWORDS = [
    "import",
    "link",
    "def",
    "declare",
    "class",
    "union",
    "enum",
    "global",
    "const",
    "return",
    "if",
    "elif",
    "else",
    "while",
    "for",
    "pass",
    "break",
    "continue",
    "True",
    "False",
    "None",
    "NULL",
    "void",
    "noreturn",
    "and",
    "or",
    "not",
    "self",
    "as",
    "sizeof",
    "assert",
    "bool",
    "byte",
    "int",
    "long",
    "float",
    "double",
    "match",
    "with",
    "case",
]


@dataclass
class Token:
    kind: str
    code: str
    location: tuple[str, int]


def tokenize(jou_code, path):
    regex = r"""
    (?P<newline> \n [ ]* )                      # a newline and all indentation after it
    | (?P<keyword> KEYWORDS )                   # "import"
    | (?P<name> [^\W\d] \w* )                   # "foo"
    | (?P<decorator> @public | @inline )
    | (?P<string> " ( \\ . | [^"\n\\] )* " )    # "hello"
    | (?P<byte> ' ( \\ . | . ) ' )              # 'a' or '\n'
    | (?P<op> \.\.\. | == | != | -> | <= | >= | \+\+ | -- | [+*/%&^|-]= | [.,:;=(){}\[\]&%*/+^<>|-] )
    | (?P<double>
        [0-9]* \. [0-9]+                        # 1.23 or .123
        | [0-9]+ \.                             # or 123.
        | (?:                                   # or:
            [0-9]* \. [0-9]+                        # 1.23 or .123
            | [0-9]+ \.                             # or 123.
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
    """.replace(
        "KEYWORDS", "|".join(r"\b" + k + r"\b" for k in KEYWORDS)
    )

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

    return tokens


BYTE_VALUES = {
    "'" + c + "'": ord(c)
    for c in string.ascii_letters
    + string.digits
    + string.punctuation.replace("\\", "").replace("'", "")
} | {
    "'\\0'": 0,
    "'\\\\'": ord("\\"),
    "'\\n'": ord("\n"),
    "'\\r'": ord("\r"),
    "'\\t'": ord("\t"),
    "'\\''": ord("'"),
    "' '": ord(" "),
}


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


def simplify_path(path: str) -> str:
    # Remove "foo/../bar" and "foo/./bar" stuff
    return os.path.relpath(os.path.abspath(path)).replace("\\", "/")


JOU_BOOL = ctypes.c_uint8


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

    def parse_type(self):
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
                    self_arg = ("self", self.parse_type(), None)
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
            return ("constant", ctypes.c_uint8(BYTE_VALUES[self.eat("byte").code]))
        if self.tokens[0].kind == "double":
            return ("constant", ctypes.c_double(float(self.eat("double").code)))
        if self.tokens[0].kind == "string":
            return ("pointer_string", unescape_string(self.eat("string").code))
        if self.tokens[0].kind == "name":
            if self.looks_like_instantiate():
                return self.parse_instantiation()
            return ("get_variable", self.eat("name").code)
        # TODO: how far are we gonna get using 0 and 1 bytes as bools?
        if self.tokens[0].code == "True":
            self.eat("True")
            return ("constant", JOU_BOOL(1))
        if self.tokens[0].code == "False":
            self.eat("False")
            return ("constant", JOU_BOOL(0))
        if self.tokens[0].code == "NULL":
            self.eat("NULL")
            return ("constant", ctypes.c_void_p())
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

    def parse_expression_with_add(self):
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

    def parse_oneline_statement(self):
        if self.tokens[0].code == "return":
            self.eat("return")
            if self.tokens[0].kind == "newline":
                return ("return", None)
            else:
                return ("return", self.parse_expression())
        if self.tokens[0].code == "assert":
            location = self.eat("assert").location
            cond = self.parse_expression()
            return ("assert", cond, location)
        if self.tokens[0].code in {"pass", "break", "continue"}:
            kw = self.tokens[0].code
            self.eat(kw)
            return (kw,)
        if self.tokens[0].code == "const":
            self.eat("const")
            name, type, value = self.parse_name_type_value()
            assert value is not None
            return ("const", name, type, value)
        if self.tokens[0].kind == "name" and self.tokens[1].code == ":":
            name, type, value = self.parse_name_type_value()
            # e.g. "foo: int"
            if self.in_class_body:
                assert value is None, self.tokens[0].location
                return ("class_field", name, type)
            else:
                return ("declare_local_var", name, type, value)

        expr = self.parse_expression()
        kind = determine_the_kind_of_a_statement_that_starts_with_an_expression(
            self.tokens[0]
        )
        if kind == "expr_stmt":
            return ("expr_stmt", expr)

        self.tokens.pop(0)
        return (kind, expr, self.parse_expression())

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
            func_name = self.eat("name").code
        else:
            func_name = None

        self.parse_start_of_body()

        cases = []
        case_underscore = None

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
        return ("match", match_obj, func_name, cases, case_underscore)

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

        if self.tokens[0].code == "import":
            assert not decors
            return self.parse_import()
        if self.tokens[0].code == "link":
            assert not decors
            self.eat("link")
            self.eat("string")
            self.eat("newline")
            return None
        if self.tokens[0].code == "def":
            return self.parse_function_or_method(decors)
        if self.tokens[0].code == "declare":
            self.eat("declare")
            if self.tokens[0].code == "global":
                self.eat("global")
                name, type, value = self.parse_name_type_value()
                assert value is None
                self.eat("newline")
                return ("global_var_declare", name, type, decors)
            else:
                signature = self.parse_function_or_method_signature()
                self.eat("newline")
                return ("func_declare",) + signature + (decors,)
        if self.tokens[0].code == "global":
            self.eat("global")
            name, type, value = self.parse_name_type_value()
            assert value is None
            self.eat("newline")
            return ("global_var_def", name, type, decors)
        if self.tokens[0].code == "class":
            return self.parse_class(decors)
        if self.tokens[0].code == "union":
            assert not decors
            return self.parse_union()
        if self.tokens[0].code == "enum":
            return self.parse_enum(decors)
        if self.tokens[0].code == "if":
            assert not decors
            return self.parse_if_statement()
        if self.tokens[0].code == "for":
            assert not decors
            return self.parse_for_loop()
        if self.tokens[0].code == "while":
            assert not decors
            return self.parse_while_loop()
        if self.tokens[0].code == "match":
            assert not decors
            return self.parse_match_statement()
        if (
            self.tokens[0].kind == "name"
            and self.tokens[1].code == ","
            and self.tokens[2].kind == "name"
        ):
            assert not decors
            return self.parse_first_of_multiple_local_var_declares()

        result = self.parse_oneline_statement()
        self.eat("newline")
        return result

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
    path = os.path.relpath(os.path.abspath(path)).replace("\\", "/")

    if path in ASTS:
        return ASTS[path]

    # print("Parsing", path)

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
    return ast


ASTS: dict[str, Any] = {}


def evaluate_compile_time_if_statements() -> None:
    for path, ast in ASTS.items():
        making_progress = True
        while making_progress:
            making_progress = False
            for i, stmt in enumerate(ast):
                if stmt[0] == "if":
                    _, if_and_elifs, else_body = stmt
                    cond_ast, then = if_and_elifs.pop(0)
                    if cond_ast[0] == "get_variable":
                        _, cond_varname = cond_ast
                        cond = find_constant(path, cond_varname)
                    elif cond_ast[0] == "constant":
                        _, cond = cond_ast
                    else:
                        raise RuntimeError(
                            f"cannot evaluate compile-time if statement in {path}"
                        )
                    if cond:
                        ast[i : i + 1] = then
                    elif not if_and_elifs:
                        ast[i : i + 1] = else_body
                    making_progress = True
                    break


FUNCTIONS: dict[str, dict[str, Any]] = {path: {} for path in ASTS}
TYPES: dict[str, dict[str | tuple[Any, ...], type[ctypes._CData]]] = {}
GLOBALS: dict[str, dict[str, type[ctypes._CData] | None]] = {}
ENUMS: dict[str, dict[str, list[str] | None]] = {}


def define_class(path, class_ast, typesub=None):
    assert class_ast[0] == "class", class_ast
    _, class_name, generics, body, decors = class_ast

    # While defining this class, refer to a dummy version of it instead.
    # This is needed for recursive pointer fields.
    class DummyClass(ctypes.Structure):
        _fields_ = []

    assert class_name not in TYPES[path]
    TYPES[path][class_name] = DummyClass

    fields = []
    for member in body:
        if member[0] == "class_field":
            _, field_name, field_type_ast = member
            field_type = type_from_ast(path, field_type_ast, typesub=typesub)
            fields.append((field_name, field_type))
        elif member[0] == "union":
            _, union_members = member
            for field_name, field_type_ast in union_members:
                field_type = type_from_ast(path, field_type_ast, typesub=typesub)
                fields.append((field_name, field_type))

    assert TYPES[path][class_name] is DummyClass
    del TYPES[path][class_name]

    class JouClass(ctypes.Structure):
        _fields_ = fields

    return JouClass


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
            result = ctypes.c_int32
            break

    if result is None:
        # Is there a class or enum definition in some imported file?
        for item in ASTS[path]:
            if item[0] == "import":
                _, path2 = item
                for item2 in ASTS[path2]:
                    if (
                        item2[0] in ("class", "enum")
                        and item2[1] == type_name
                        and "@public" in item2[-1]
                    ):
                        result = find_named_type(path2, type_name)
                        break
                if result is not None:
                    break

    if result is None:
        raise RuntimeError(f"type not found: {type_name}")

    TYPES[path][type_name] = result
    return result


def find_generic_class(path: str, class_name: str, generic_param: type):
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
                _, path2 = item
                for item2 in ASTS[path2]:
                    if item2[:2] == ("class", class_name) and "@public" in item2[-1]:
                        result = find_generic_class(path2, class_name, generic_param)
                        break
                if result is not None:
                    break

    if result is None:
        raise RuntimeError(f"generic class not found: {class_name}")

    TYPES[path][class_name, generic_param] = result
    return result


def declare_c_function(path, declare_ast):
    _, func_name, args, takes_varargs, return_type, decors = declare_ast
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
    func.argtypes = [type_from_ast(path, triple[1]) for triple in args]
    if return_type == ("named_type", "None") or return_type == (
        "named_type",
        "noreturn",
    ):
        func.restype = None
    else:
        func.restype = type_from_ast(path, return_type)

    return func


def declare_c_global_var(path, declare_ast):
    assert declare_ast[0] == "global_var_declare"
    _, varname, vartype_ast, decors = declare_ast
    print("Declaring global variable", varname)
    vartype = type_from_ast(path, vartype_ast)

    for lib in LIBS:
        try:
            return vartype.in_dll(lib, varname)
        except ValueError:
            pass

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
    result = None
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
                _, path2 = item
                for item2 in ASTS[path2]:
                    if (
                        item2[0] in ("function_def", "func_declare")
                        and item2[1] == func_name
                        and "@public" in item2[-1]
                    ):
                        result = find_function(path2, func_name)
                        assert result is not None
                        break
                if result is not None:
                    break

    # may assign None, that's fine, next time we know it's not a thing
    FUNCTIONS[path][func_name] = result
    return result


def find_constant(path, name):
    if name == "WINDOWS":
        return JOU_BOOL(int(sys.platform == "win32"))
    if name == "MACOS":
        return JOU_BOOL(int(sys.platform == "darwin"))
    if name == "NETBSD":
        return JOU_BOOL(int(sys.platform.startswith("netbsd")))
    # TODO: `const` constants
    return None


def find_global_var(path, varname):
    try:
        return GLOBALS[path][varname]
    except KeyError:
        pass

    # Is it defined or declared in this file?
    result = None
    for ast in ASTS[path]:
        if ast[:2] == ("global_var_def", varname):
            _, _, var_type, decors = ast
            # Create new global variable
            result = type_from_ast(path, var_type)()
            break
        if ast[:2] == ("global_var_declare", varname):
            result = declare_c_global_var(path, ast)
            break

    if result is None:
        # Is it defined or declared in an imported file?
        for item in ASTS[path]:
            if item[0] == "import":
                _, path2 = item
                for item2 in ASTS[path2]:
                    if (
                        item2[0] in ("global_var_def", "global_var_declare")
                        and item2[1] == varname
                        and "@public" in item2[-1]
                    ):
                        result = find_global_var(path2, varname)
                        assert result is not None
                        break
                if result is not None:
                    break

    # may assign None, that's fine, next time we know it's not a thing
    GLOBALS[path][varname] = result
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
            _, _, members, decors = ast
            result = members
            break

    if result is None:
        # Is it defined or declared in an imported file?
        for item in ASTS[path]:
            if item[0] == "import":
                _, path2 = item
                for item2 in ASTS[path2]:
                    if item2[:2] == ("enum", enum_name) and "@public" in item2[-1]:
                        result = find_enum(path2, enum_name)
                        assert result is not None
                        break
                if result is not None:
                    break

    # may assign None, that's fine, next time we know it's not a thing
    ENUMS[path][enum_name] = result
    return result


def type_from_ast(path, ast, typesub=None):
    if ast[0] == "pointer":
        _, value_type_ast = ast
        if value_type_ast == ("named_type", "void"):
            return ctypes.c_void_p
        return ctypes.POINTER(type_from_ast(path, value_type_ast, typesub=typesub))
    if ast[0] == "named_type":
        _, name = ast
        if typesub is not None and name in typesub:
            return typesub[name]
        return find_named_type(path, name)
    if ast[0] == "array":
        _, item_type_ast, length_ast = ast
        assert length_ast[0] == "integer_constant"
        _, length = length_ast
        return type_from_ast(path, item_type_ast, typesub=typesub) * length
    if ast[0] == "generic":
        _, class_name, [param_type_ast] = ast
        param_type = type_from_ast(path, param_type_ast, typesub=typesub)
        return find_generic_class(path, class_name, param_type)
    raise NotImplementedError(ast)


# This happens implicitly a lot when bytes are passed around.
#
# Python requires this to be explicit: `ctypes.c_int` for example is actually a
# pointer to an int and without this it can confusingly change in many places.
def shallow_copy(value):
    result = type(value)()
    ctypes.pointer(result)[0] = value
    return result


def is_pointer_type(ctype):
    return (
        # TODO: There doesn't seem to be a good way to do this.
        ctype in (ctypes.c_char_p, ctypes.c_wchar_p, ctypes.c_void_p)
        or isinstance(ctype, (ctypes._Pointer, ctypes._CFuncPtr))
    )


def is_array(ctype):
    return hasattr(ctype, "_length_")


INTEGER_TYPES = {
    "int8": ctypes.c_int8,
    "int16": ctypes.c_int16,
    "int32": ctypes.c_int32,
    "int64": ctypes.c_int64,
    "uint8": ctypes.c_uint8,
    "uint16": ctypes.c_uint16,
    "uint32": ctypes.c_uint32,
    "uint64": ctypes.c_uint64,
}


def ctype_to_string(ctype):
    if ctype == ctypes.c_void_p:
        return "void*"

    for int_name, int_type in INTEGER_TYPES.items():
        if int_type == ctype:
            return int_name

    if hasattr(ctype, "_length_") and hasattr(ctype, "_type_"):
        # It is array
        array_len = ctype._length_
        assert isinstance(array_len, int)
        return ctype_to_string(ctype._type_) + "[" + str(array_len) + "]"

    if hasattr(ctype, "_type_"):
        # It is a pointer
        return ctype_to_string(ctype._type_) + "*"

    if hasattr(ctype, "_fields_"):
        # It is a class
        return (
            "class("
            + ", ".join(ctype_to_string(ftype) for fname, ftype in ctype._fields_)
            + ")"
        )

    breakpoint()
    raise NotImplementedError(ctype)


def cast_or_copy(value, to):
    from_str = ctype_to_string(type(value))
    to_str = ctype_to_string(to)

    if from_str == to_str:
        return shallow_copy(value)

    if from_str.endswith(("*", "]")) and to_str.endswith("*"):
        # Simple pointer-to-pointer cast or array-to-pointer cast
        return ctypes.cast(value, to)

    if from_str == "uint8*" and re.fullmatch(r"uint8\[\d+\]", to_str):
        # String from pointer to array (only allowed in some special cases)
        array = to()
        LIBS[0].strcpy(value, array)
        return array

    if from_str in INTEGER_TYPES and to_str in INTEGER_TYPES:
        return to(value.value)

    raise NotImplementedError(from_str, to_str)


def get_field(instance, field_name):
    # No idea why ctypes makes this so difficult...
    # The problem is that `getattr(instance, field)` returns e.g. Python `int`, not `c_int`
    # https://stackoverflow.com/a/50534262
    struct = type(instance)
    field_type = next(ftype for fname, ftype in struct._fields_ if fname == field_name)
    field_offset = getattr(struct, field_name).offset
    return field_type.from_buffer(instance, field_offset)


def unwrap_value(obj):
    if hasattr(obj, "value"):
        # e.g. ctypes.c_int
        return obj.value
    else:
        # assume it is a pointer
        return ctypes.cast(obj, ctypes.c_void_p).value


class Return(Exception):
    pass


class Break(Exception):
    pass


class Runner:
    def __init__(self, path: str) -> None:
        self.path = path
        self.locals: dict[str, ctypes._CData] = {}

    def run_body(self, body):
        for item in body:
            self.run_statement(item)

    def find_any_var_or_constant(self, varname):
        constant = find_constant(self.path, varname)
        if constant is not None:
            return constant

        if varname in self.locals:
            return self.locals[varname]

        return find_global_var(self.path, varname)

    def run_statement(self, stmt):
        if stmt[0] == "expr_stmt":
            _, expr = stmt
            self.run_expression(stmt[1])
        elif stmt[0] == "if":
            _, if_and_elifs, otherwise = stmt
            for cond, body in if_and_elifs:
                if self.run_expression(cond):
                    self.run_body(body)
                    break
            else:
                self.run_body(otherwise)
        elif stmt[0] == "assign":
            _, target_ast, value_ast = stmt
            if target_ast[0] == "get_variable":
                _, varname = target_ast
                if self.find_any_var_or_constant(varname) is None:
                    # Making a new variable. Use the type of the value being assigned.
                    self.locals[varname] = shallow_copy(self.run_expression(value_ast))
                    return
            target = self.run_expression(target_ast)
            value = self.run_expression(value_ast)
            ctypes.pointer(target)[0] = cast_or_copy(value, type(target))
        elif stmt[0] == "in_place_mul":
            _, target_ast, value_ast = stmt
            target = self.run_expression(target_ast)
            value = self.run_expression(value_ast)
            ctypes.pointer(target)[0] *= value.value
        elif stmt[0] == "declare_local_var":
            _, varname, type_ast, value_ast = stmt
            vartype = type_from_ast(self.path, type_ast)
            var = vartype()
            if value_ast is not None:
                value = self.run_expression(value_ast)
                ctypes.pointer(var)[0] = cast_or_copy(value, vartype)
            self.locals[varname] = var
        elif stmt[0] == "assert":
            _, cond, location = stmt
            if not self.run_expression(cond):
                raise RuntimeError(f"assertion failed in {location}")
        elif stmt[0] == "return":
            _, val = stmt
            raise Return(self.run_expression(val))
        elif stmt[0] == "for":
            _, init, cond, incr, body = stmt
            self.run_statement(init)
            while self.run_expression(cond).value:
                try:
                    self.run_body(body)
                except Break:
                    break
                self.run_statement(incr)
        elif stmt[0] == "while":
            _, cond, body = stmt
            while self.run_expression(cond).value:
                try:
                    self.run_body(body)
                except Break:
                    break
        elif stmt[0] == "break":
            raise Break()
        else:
            raise NotImplementedError(stmt)

    # Returns a value that may be converted to pointer if needed, i.e. usually
    # not implicitly copied.
    def run_expression(self, expr):
        if expr[0] == "call":
            _, func_ast, arg_asts = expr
            func = self.run_expression(func_ast)
            args_iter = (self.run_expression(arg_ast) for arg_ast in arg_asts)

            if func_ast[0] == "get_variable":
                _, funcname = func_ast
            else:
                funcname = str(func_ast)  # good enough lol

            if isinstance(func, tuple):
                print("Calling Jou function:", funcname)
            else:
                print("Calling C function:", funcname)
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

        elif expr[0] == "eq":
            left, right = (unwrap_value(self.run_expression(ast)) for ast in expr[1:])
            return JOU_BOOL(left == right)

        elif expr[0] == "ne":
            left, right = (unwrap_value(self.run_expression(ast)) for ast in expr[1:])
            return JOU_BOOL(left != right)

        elif expr[0] == "gt":
            left, right = (unwrap_value(self.run_expression(ast)) for ast in expr[1:])
            return JOU_BOOL(left > right)

        elif expr[0] == "lt":
            left, right = (unwrap_value(self.run_expression(ast)) for ast in expr[1:])
            return JOU_BOOL(left < right)

        elif expr[0] == "ge":
            left, right = (unwrap_value(self.run_expression(ast)) for ast in expr[1:])
            return JOU_BOOL(left >= right)

        elif expr[0] == "le":
            left, right = (unwrap_value(self.run_expression(ast)) for ast in expr[1:])
            return JOU_BOOL(left <= right)

        elif expr[0] == "sizeof":
            _, obj = expr
            # TODO: sizeof() shouldn't run the thing, just get its type
            return ctypes.c_int64(ctypes.sizeof(self.run_expression(obj)))

        elif expr[0] == ".":
            # TODO: can be many other things than plain old field access!
            _, obj_ast, field_name = expr
            if obj_ast[0] == "get_variable":
                _, obj_name = obj_ast
                enum = find_enum(self.path, obj_name)
                if enum is not None:
                    # It is Enum.Member, not instance.field
                    return ctypes.c_int(enum.index(field_name))
            return get_field(self.run_expression(obj_ast), field_name)

        elif expr[0] == "constant":
            _, value = expr
            return shallow_copy(value)

        elif expr[0] == "integer_constant":
            # TODO: type hints
            _, value = expr
            return ctypes.c_int(value)

        elif expr[0] == "address_of":
            _, obj = expr
            return ctypes.pointer(self.run_expression(obj))

        elif expr[0] == "pointer_string":
            _, data = expr
            array_size = len(data) + 1
            array = (ctypes.c_uint8 * array_size)(*data)
            return ctypes.cast(ctypes.pointer(array), ctypes.POINTER(ctypes.c_uint8))

        elif expr[0] == "[":
            _, obj_ast, index_ast = expr
            obj = self.run_expression(obj_ast)
            index = self.run_expression(index_ast)
            # TODO: implicit array to pointer casts
            # TODO: how does this handle `&foo[out_of_bounds]`? probably not right?
            return obj[index.value]

        elif expr[0] == "instantiate":
            _, type_ast, fields = expr
            print("Inst", type_ast)
            t = type_from_ast(self.path, type_ast)
            kwargs = {}
            for field_name, field_value in fields:
                field_value = self.run_expression(field_value)
                field_type = next(
                    ftype for fname, ftype in t._fields_ if fname == field_name
                )
                kwargs[field_name] = cast_or_copy(field_value, field_type)
            return t(**kwargs)

        elif expr[0] == "as":
            _, obj_ast, type_ast = expr
            obj = self.run_expression(obj_ast)
            t = type_from_ast(self.path, type_ast)
            return cast_or_copy(obj, t)

        elif expr[0] == "and":
            _, lhs, rhs = expr
            return JOU_BOOL(self.run_expression(lhs).value and self.run_expression(rhs).value)

        elif expr[0] == "or":
            _, lhs, rhs = expr
            return JOU_BOOL(self.run_expression(lhs).value or self.run_expression(rhs).value)

        elif expr[0] == "not":
            _, inner = expr
            return JOU_BOOL(not self.run_expression(inner).value)

        elif expr[0] == "div":
            _, lhs_ast, rhs_ast = expr
            lhs = self.run_expression(lhs_ast)
            rhs = self.run_expression(rhs_ast)
            assert type(lhs) == type(rhs)  # TODO: figure out type properly
            return type(lhs)(lhs.value // rhs.value)

        elif expr[0] == "post_incr":
            _, obj_ast = expr
            obj = self.run_expression(obj_ast)
            result = shallow_copy(obj)
            obj.value += 1
            return result

        else:
            raise NotImplementedError(expr)


# Args must be given as an iterator to get the right evaluation order AND type
# conversions for arguments. When we evaluate an argument, we need to cast it
# before we evaluate the next argument.
def call_function(func, args_iter):
    assert func is not None
    if not isinstance(func, tuple):
        # Function defined in C and declared in Jou
        args = []
        for arg_type in func.argtypes or []:
            args.append(cast_or_copy(next(args_iter), arg_type))
        # Handle varargs: printf("hello %d\n", 1, 2, 3)
        # TODO: use something else than shallow copy for varargs
        args.extend(args_iter)
        result = func(*args)
        if isinstance(result, int):
            result = func.restype(result)
        return result

    # Function defined in Jou
    func_path, func_ast = func
    assert func_ast[0] == "function_def"
    _, func_name, funcdef_args, takes_varargs, return_type, body, decors = func_ast
    assert not takes_varargs

    r = Runner(func_path)

    for arg_name, arg_type, arg_default in funcdef_args:
        assert arg_default is None
        r.locals[arg_name] = cast_or_copy(
            next(args_iter), type_from_ast(func_path, arg_type)
        )

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
    LIBS.append(ctypes.CDLL(ctypes.util.find_library("c")))
    LIBS.append(ctypes.CDLL(ctypes.util.find_library("m")))
    LIBS.extend(load_llvm())

    print("Parsing Jou files...", end=" ", flush=True)
    parse_file("compiler/main.jou")
    print(f"Parsed {len(ASTS)} files.")

    for path in ASTS.keys():
        TYPES[path] = {
            "byte": ctypes.c_uint8,
            "int": ctypes.c_int32,
            "long": ctypes.c_int64,
            "float": ctypes.c_float,
            "double": ctypes.c_double,
            "bool": JOU_BOOL,
        } | INTEGER_TYPES
        GLOBALS[path] = {}
        FUNCTIONS[path] = {}
        ENUMS[path] = {}

    print("Evaluating compile-time if statements...")
    evaluate_compile_time_if_statements()

    args = []
    for arg in "jou -vv -o jou_bootstrap compiler/main.jou".split():
        python_bytes = arg.encode("utf-8")
        c_string = ctypes.c_char_p(python_bytes)
        jou_string = ctypes.POINTER(ctypes.c_uint8)(c_string)
        args.append(jou_string)

    argc = ctypes.c_int(len(args))
    argv = (ctypes.POINTER(ctypes.c_uint8) * (len(args) + 1))(*args, None)
    call_function(
        func=find_function("compiler/main.jou", "main"),
        args_iter=iter([argc, argv]),
    )


if __name__ == "__main__":
    main()
