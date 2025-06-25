import string
import ctypes
import re
from pathlib import Path
from dataclasses import dataclass

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
    location: tuple[Path, int]


def tokenize(jou_code, path):
    regex = r"""
    (?P<newline> \n [ ]* )                      # a newline and all indentation after it
    | (?P<keyword> KEYWORDS )                   # e.g. "import"
    | (?P<name> [^\W\d] \w* )                   # e.g. "foo"
    | (?P<decorator> @public | @inline )
    | (?P<string> " ( \\ . | [^"\n\\] )* " )    # e.g. "hello"
    | (?P<byte> ' ( \\ . | . ) ' )              # e.g. 'a' or '\n'
    | (?P<op> \.\.\. | == | != | -> | <= | >= | \+\+ | -- | [+*/%&^|-]= | [.,:;=(){}\[\]&%*/+^<>|-] )
    | (?P<int2> 0b [01_]+ )                     # e.g. 0b0100_1010
    | (?P<int8> 0o [0-7_]+ )                    # e.g. 0o777
    | (?P<int16> 0x [0-9A-Fa-f_]+ )             # e.g. 0xFFFF_FFFF
    | (?P<int10> [0-9] [0-9_]* )                # e.g. 123
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
            tokens.append(
                Token(kind=m.lastgroup, code=m.group(0), location=(path, lineno))
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
    "'\\n'": ord('\n'),
    "'\\r'": ord('\r'),
    "'\\t'": ord('\t'),
    "'\\''": ord("'"),
    "' '": ord(' '),
}


# reverse code golfing: https://xkcd.com/1960/
def determine_the_kind_of_a_statement_that_starts_with_an_expression(t):
    if t.code == ("="):
        return "assign"
    if t.code == ("+="):
        return "in_place_add"
    if t.code == ("-="):
        return "in_place_sub"
    if t.code == ("*="):
        return "in_place_mul"
    if t.code == ("/="):
        return "in_place_div"
    if t.code == ("%="):
        return "in_place_mod"
    if t.code == ("&="):
        return "in_place_bit_and"
    if t.code == ("|="):
        return "in_place_bit_or"
    if t.code == ("^="):
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


class Parser:
    def __init__(self, path: Path, tokens: list[Token]):
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
            path = (self.path.parent / path_spec).resolve()
        else:
            assert path_spec.startswith("stdlib/")
            path = Path(path_spec).resolve()
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
        result = ("named_type", t.code)

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

    def parse_call(self):
        assert self.tokens[0].kind == "name"
        name = self.tokens[0].code
        self.eat("name")
        self.eat("(")

        args = []
        while self.tokens[0].code != ")":
            args.append(self.parse_expression())
            if self.tokens[0].code != ",":
                break
            self.eat(",")
        self.eat(")")
        return ("call", name, args)

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
        if self.tokens[0].kind in {"int2", "int8", "int10", "int16"}:
            return ("integer_constant", int(self.tokens.pop(0).code.replace("_", ""), 0))
        if self.tokens[0].kind == "byte":
            return ("constant", ctypes.c_uint8(BYTE_VALUES[self.eat("byte").code]))
        #            case TokenKind.Float:
        #                expr.kind = AstExpressionKind.Constant
        #                expr.constant = Constant{kind = ConstantKind.Float, float_or_double_text = self.tokens[0].short_string}
        #                self.tokens++
        #            case TokenKind.Double:
        #                expr.kind = AstExpressionKind.Constant
        #                expr.constant = Constant{kind = ConstantKind.Double, float_or_double_text = self.tokens[0].short_string}
        #                self.tokens++
        if self.tokens[0].kind == "string":
            return ("pointer_string", unescape_string(self.eat("string").code))
        if self.tokens[0].kind == "name":
            if self.tokens[1].code == "(":
                return self.parse_call()
            if self.looks_like_instantiate():
                return self.parse_instantiation()
            return ("get_variable", self.eat("name").code)
        # TODO: how far are we gonna get using 0 and 1 bytes as bools?
        if self.tokens[0].code == "True":
            self.eat("True")
            return ("constant", ctypes.c_uint8(1))
        if self.tokens[0].code == "False":
            self.eat("False")
            return ("constant", ctypes.c_uint8(0))
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

    def parse_expression_with_fields_and_methods_and_indexing(self):
        result = self.parse_elementary_expression()

        while self.tokens[0].code in {".", "->", "["}:
            if self.tokens[0].code == "[":
                self.eat("[")
                index = self.parse_expression()
                self.eat("]")
                result = ("indexing", result, index)
            else:
                arrow_or_dot = self.tokens.pop(0).code
                assert self.tokens[0].kind == "name"
                if self.tokens[1].code == "(":
                    _, name, args = self.parse_call()
                    result = ("call_method", result, arrow_or_dot, name, args)
                else:
                    name = self.eat("name").code
                    result = ("get_class_field", result, arrow_or_dot, name)

        return result

    def parse_expression_with_unary_operators(self):
        prefix = []
        while self.tokens[0].code in {"++", "--", "&", "*", "sizeof"}:
            prefix.append(self.tokens.pop(0))

        result = self.parse_expression_with_fields_and_methods_and_indexing()

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
            result = ("negate", self.parse_expression_with_mul_and_div())
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
                return ("func_declare", signature, decors)
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

        return ("function_def", signature, body)

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


asts = {}


def parse_file(path):
    path = path.resolve()
    if path in asts:
        return asts[path]

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

    asts[path] = ast
    for imp_path in imported:
        parse_file(imp_path)
    return ast


parse_file(Path("compiler/main.jou"))
