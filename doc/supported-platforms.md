# Supported Platforms

This page documents which platforms Jou works on.


## Operating systems

|         | x86 (32‑bit) | x86_64 (64-bit) | arm (32-bit) | aarch64 (64-bit) |
|---------|--------------|--------|------------|---------|
| Windows | ❌ | ✅ | ❌ | ❌ |
| Linux   | ✅ | ✅ | ✅ | ✅ |
| MacOS   | ❌ | ✅ | ❌ | 🤷 |
| NetBSD  | 🤷 | ✅ | 🤷 | 🤷 |

where:
- ✅ = supported
- 🤷 = probably works fine, but not guaranteed
- ❌ = not supported

If you would like to use Jou on a 🤷 or ❌ platform,
or the platform isn't even mentioned here,
please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new).


## Embedded devices

No microcontrollers are currently supported,
although I would like to eventually support at least the Raspberry Pi Pico.

Regular Raspberry Pi's (not Pico) are supported,
because they run a Linux operating system much like a typical desktop/laptop PC.


## Built-in constants

These constants are always available, even if you don't import anything.
Use them if you need to write platform-specific code.
For example, maybe you have a small amount of non-essential code that doesn't work on Windows, and you put it under `if not WINDOWS:`.
Or maybe you have an entire file of Windows-only code and you place `assert WINDOWS` at the start of that file
(see [the `assert` documentation](assert.md)).

Operating system:
- `WINDOWS: bool`
- `LINUX: bool`
- `MACOS: bool`
- `NETBSD: bool`

32-bit vs 64-bit (others are not supported):
- `IS_32BIT: bool` (`False` means 64-bit. See also the `intnative` type in [the integer types documentation](types.md#integers).)
