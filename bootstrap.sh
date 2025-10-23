#!/usr/bin/env bash
#
# The Jou compiler is written in Jou, but it doesn't help you very much if you
# have nothing that can compile or run Jou code. That's why this script exists.
#
# This script uses `jou_to_c.py` to convert the Jou compiler's source code to C
# and then compiles that with a C compiler.
#
# The Jou compiler version to convert is not whatever is a hard-coded commit,
# not whatever is on the the latest `main` branch, for a few reasons:
#   - Adding a feature to Jou does not necessarily break this script.
#   - When you develop Jou, this process does not need to happen every time you
#     change the compiler's source code.
#   - If there's something wrong with the compiler, this script will likely
#     give you a cryptic error message. By using a known-to-be-good commit, we
#     avoid that.


set -e

if [ -z "$LLVM_CONFIG" ] && ! [[ "$OS" =~ Windows ]]; then
    echo "Please set the LLVM_CONFIG environment variable. Otherwise different"
    echo "Jou commits may use different LLVM versions, and it gets confusing."
    echo ""
    echo "The easiest way to set LLVM_CONFIG is to run this script with 'make'."
    exit 1
fi

if [[ "${OS:=$(uname)}" =~ Windows ]]; then
    source activate
    make="mingw32-make"
    exe_suffix=".exe"
else
    if [[ "$OS" =~ NetBSD ]]; then
        make="gmake"
    else
        make="make"
    fi
    exe_suffix=""
fi


echo "$0: Finding Python..."

if [[ "${OS:=$(uname)}" =~ Windows ]]; then
    # Avoid the "python.exe" launcher that opens app store for installing python.
    python=$( (command -v py python || true) | grep -v Microsoft/WindowsApps | head -1)
else
    python=$( (command -v python3.13 python3 python || true) | head -1)
fi

if ! [[ "$("$python" --version || true)" =~ ^Python\ 3 ]]; then
    echo -e "\x1b[31mError: Python not found. Please install it.\x1b[0m" >&2
    echo "Also, please create an issue on GitHub if you can't get this to work." >&2
    exit 1
fi

commit=d89c4f5a33ac9a6be7b2dcd66d6f2dca695452bc

if [ -f tmp/bootstrap/jou_bootstrap_$commit ]; then
    echo "$0: Found tmp/bootstrap/jou_bootstrap_$commit. Delete the tmp/bootstrap folder if you want to bootstrap again."
else
    echo "$0: Checking out commit $commit in temporary folder..."

    # TODO: delete the old tmp/bootstrap_cache folder if it exists? It can be huge.
    rm -rf tmp/bootstrap
    mkdir -p tmp/bootstrap

    # This seems to be the best way to checkout a commit into a folder.
    git archive --format=tar $commit | (cd tmp/bootstrap && tar xf -)
    if [ -f config.jou ]; then cp config.jou tmp/bootstrap/; fi

    echo "$0: Converting Jou compiler to C code..."
    "$python" jou_to_c.py tmp/bootstrap/compiler/main.jou > tmp/bootstrap/compiler.c

    echo "$0: Compiling C code..."

    if [[ "${OS:=$(uname)}" =~ Windows ]]; then
        cc=mingw64/bin/gcc.exe  # TODO: This will not work with --small!
        cflags="$(echo mingw64/lib/libLLVM*.dll.a) mingw64/lib/libLTO.dll.a"
    else
        cc="$(which `$LLVM_CONFIG --bindir`/clang || which clang)"
        cflags="$($LLVM_CONFIG --ldflags --libs)"
    fi
    $cc -w -O1 -x c tmp/bootstrap/compiler.c -o tmp/bootstrap/jou_bootstrap_$commit$exe_suffix $cflags
fi

echo -n "$0: Copying the bootstrapped executable... "
cp -v tmp/bootstrap/jou_bootstrap_$commit$exe_suffix ./jou_bootstrap$exe_suffix

echo "$0: Done!"
