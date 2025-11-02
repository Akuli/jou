import struct
import random

# AI generated
def utf16_to_wtf8(utf16: list[int]) -> bytes:
    result = bytearray()
    i = 0
    while i < len(utf16):
        unit = utf16[i]
        if 0xD800 <= unit <= 0xDBFF and i + 1 < len(utf16):
            next_unit = utf16[i + 1]
            if 0xDC00 <= next_unit <= 0xDFFF:
                # Valid surrogate pair → combine into code point
                high, low = unit, next_unit
                codepoint = 0x10000 + (((high - 0xD800) << 10) | (low - 0xDC00))
                result.extend(chr(codepoint).encode('utf-8'))
                i += 2
                continue
        if 0xD800 <= unit <= 0xDFFF:
            # Lone surrogate → CESU-8 style 3-byte encoding
            result.extend([
                0xE0 | ((unit >> 12) & 0x0F),
                0x80 | ((unit >> 6) & 0x3F),
                0x80 | (unit & 0x3F)
            ])
        else:
            result.extend(chr(unit).encode('utf-8'))
        i += 1
    return bytes(result)

# AI generated
def wtf8_to_utf16(wtf8: bytes) -> list[int]:
    i = 0
    utf16 = []
    while i < len(wtf8):
        byte = wtf8[i]
        if byte < 0x80:
            utf16.append(byte)
            i += 1
        elif (byte & 0xE0) == 0xC0:
            b1, b2 = wtf8[i], wtf8[i+1]
            codepoint = ((b1 & 0x1F) << 6) | (b2 & 0x3F)
            utf16.append(codepoint)
            i += 2
        elif (byte & 0xF0) == 0xE0:
            b1, b2, b3 = wtf8[i], wtf8[i+1], wtf8[i+2]
            codepoint = ((b1 & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F)
            utf16.append(codepoint)
            i += 3
        elif (byte & 0xF8) == 0xF0:
            b1, b2, b3, b4 = wtf8[i], wtf8[i+1], wtf8[i+2], wtf8[i+3]
            codepoint = ((b1 & 0x07) << 18) | ((b2 & 0x3F) << 12) | ((b3 & 0x3F) << 6) | (b4 & 0x3F)
            # Convert to surrogate pair
            codepoint -= 0x10000
            high = 0xD800 + ((codepoint >> 10) & 0x3FF)
            low = 0xDC00 + (codepoint & 0x3FF)
            utf16.extend([high, low])
            i += 4
        else:
            raise ValueError(f"Invalid WTF-8 byte at position {i}")
    return utf16

for i in range(10000):
    # Preserves arbitrary data
    u16 = [random.randint(0, 0xFFFF) for i in range(100)]
    w8 = utf16_to_wtf8(u16)
    assert wtf8_to_utf16(w8) == u16

    # Maps valid UTF-16 to valid UTF-8 and valid UTF-8 to valid UTF-16
    string = random.randbytes(100).decode("utf-16-le", errors="ignore") + random.randbytes(100).decode("utf-8", errors="ignore")
    u16_bytes = string.encode("utf-16-le")
    u16 = list(struct.unpack("H" * (len(u16_bytes) // 2), u16_bytes))
    assert utf16_to_wtf8(u16) == string.encode("utf-8")
    assert wtf8_to_utf16(string.encode("utf-8")) == u16

print("ok")
