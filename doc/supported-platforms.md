# Supported Platforms

This page documents which platforms Jou works on.


## Operating systems

|         | x86 (32‑bit) | x86_64 (64-bit) | arm (32-bit) | aarch64 (64-bit) |
|---------|--------------|--------|------------|---------|
| Windows 7 (see note 1 below) | ❌ | ✅ | ❌ | ❌ |
| Windows 8 or newer | ❌ | ✅ | ❌ | ❌ |
| Linux (see note 2 below) | ✅ | ✅ | ✅ | ✅ |
| MacOS   | ❌ | ✅ | ❌ | 🤷 |
| NetBSD 10 | 🤷 | ✅ | 🤷 | 🤷 |
| NetBSD 11 | 🤷 | 🤷 | 🤷 | 🤷 |

where:
- ✅ = supported
- 🤷 = probably works fine, but not tested regularly
- ❌ = not supported

Platform-specific notes:
1. `jou --update` doesn't work on Windows 7.
2. Other libc's than glibc and musl might not work.
   For example, if I remember correctly, Jou doesn't work on Android phones.

If you would like to use Jou on a 🤷 or ❌ platform,
or the platform isn't even mentioned here,
please [create an issue on GitHub](https://github.com/Akuli/jou/issues/new).


## Embedded devices

No microcontrollers are currently supported,
although I would like to eventually support at least Raspberry Pi Pico.

Regular Raspberry Pi's (not Pico) starting from RasPi 1 are supported,
because they run a Linux operating system much like a typical PC (except that it's arm, not x86).


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
