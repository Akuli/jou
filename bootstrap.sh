#!/usr/bin/env bash
#
# The Jou compiler is written in Jou, but it doesn't help you very much if you
# have nothing that can compile or run Jou code. That's why this script exists.
#
# See the bootstrapping section in README.md for details.
#
# If you just want to setup a Jou development environment, you probably don't
# need to run this script manually, because `make` runs it as needed.

set -e -o pipefail

# Add the latest commit on main branch to the end of the list if this script
# produces a compiler that is too old. This means that whatever the script is
# currently producing will be used to compile the commit you add.
#
# The numbering does not start from 0 for historical reasons. Commit 001 was
# just before the original compiler written in C was deleted.
x=$(git rev-parse origin/main^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^)
numbered_commits=(
    016_98c5fb2792eaac8bbe7496a176808d684f631d82  # <--- "./windows_setup.sh --small" starts from here! (release 2025-04-08-2200)
    017_eed3b974ccb42a01339ead7f6dcaa0913ca2cd64  # fixed-size integer types, e.g. uint64
    018_c594c326d3031a2894731f60ac9881a206793dfa  # infer types of integers in code
    019_99de2976a7f3b34ec6b2b07725c5ad1400313dc1  # bitwise xor operator `^`
    020_944c0f34e941d340af1749cdceea4621860ec69f  # bitwise '&' and '|', "const" supports integers other than int
    021_9339a749315b82f73d19bacdccab5ee327c44822  # accessing fields and methods on pointers with '.' instead of '->'
    022_e35573c899699e2d717421f3bcd29e16a5a35cc1  # bootstrap_transpiler.py used to start here, maybe not needed now...
    023_525d0c746286bc9004c90173503e47e34010cc6a  # function pointers, no more automagic stdlib importing for io or assert
    024_0d4b4082f6569131903af02ba5508210b8b474d8  # TODO: write something here
    025_$x
    026_5c60bc1f68efb3f957730bd97eb4607415368dd4  # Ideally bootstrap_transpiler.py would start here...
)

# This should be an item of the above list according to what
# bootstrap_transpiler.py supports.
bootstrap_transpiler_numbered_commit=025_$x

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

CYAN="\x1b[36m"
RED="\x1b[31m"
RESET="\x1b[0m"

# On windows, you get a linker error if you try to compile LLVM code without these files.
#
# The same list of files is in:
#   - .github/workflows/windows.yml
#   - compiler/llvm.jou
windows_llvm_files=(
    mingw64/lib/libLLVMCore.dll.a
    mingw64/lib/libLLVMX86CodeGen.dll.a
    mingw64/lib/libLLVMAnalysis.dll.a
    mingw64/lib/libLLVMTarget.dll.a
    mingw64/lib/libLLVMPasses.dll.a
    mingw64/lib/libLLVMSupport.dll.a
    mingw64/lib/libLLVMLinker.dll.a
    mingw64/lib/libLTO.dll.a
    mingw64/lib/libLLVMX86AsmParser.dll.a
    mingw64/lib/libLLVMX86Info.dll.a
    mingw64/lib/libLLVMX86Desc.dll.a
)

function transpile_with_python_and_compile() {
    echo -e "${CYAN}$0: Let's use Python to convert a Jou compiler to C code.${RESET} "
    echo -n "Finding Python... "
    if [[ "${OS:=$(uname)}" =~ Windows ]]; then
        # Avoid the "python.exe" launcher that opens app store for installing python.
        python=$( (command -v py python || true) | (grep -v Microsoft/WindowsApps || true) | head -1)
    else
        python=$( (command -v python3 python python3.{10..20} || true) | head -1)
    fi
    echo "$python"

    if ! [[ "$("$python" --version || true)" =~ ^Python\ 3 ]]; then
        echo -e "${RED}Error: Python not found. Please install it.${RESET}" >&2
        echo "Also, please create an issue on GitHub if you can't get this to work." >&2
        exit 1
    fi

    echo -n "Finding clang... "
    if [[ "${OS:=$(uname)}" =~ Windows ]]; then
        clang="$PWD/mingw64/bin/clang.exe"
    else
        clang="$(command -v $($LLVM_CONFIG --bindir)/clang || command -v clang)"
    fi
    echo "$clang"

    if ! [ -f "$clang" ]; then
        echo -e "${RED}Error: clang not found.${RESET}" >&2
        echo "Please create an issue on GitHub if you can't get this to work." >&2
        exit 1
    fi

    local folder=tmp/bootstrap_cache/$bootstrap_transpiler_numbered_commit
    local number=${bootstrap_transpiler_numbered_commit%%_*}
    local commit=${bootstrap_transpiler_numbered_commit#*_}

    echo "Checking out commit $number into $folder"
    rm -rf $folder
    mkdir -p $folder
    git archive --format=tar $commit | (cd $folder && tar xf -)
    cp -v config.jou $folder || true

    if [[ "$OS" =~ Windows ]]; then
        echo "Copying LLVM files..."
        mkdir -p $folder/mingw64/lib
        cp -v ${windows_llvm_files[@]} $folder/mingw64/lib/
    fi

    (
        cd $folder

        if [[ "$OS" =~ Windows ]]; then
            # An if statement inside a function should get evaluated at compile time, but it doesn't
            echo "Patching Jou code to assume aarch64 support exists..."
            grep LLVM_HAS_AARCH64 compiler/target.jou  # fail if there's nothing to replace
            sed -i s/LLVM_HAS_AARCH64/True/g compiler/target.jou
        fi

        # Compiler uses utf8_encode_char() to detect bad characters in Jou code.
        # Let's make it return something that is not in the code so it finds no bad characters.
        #
        # We can't just compile with the original stdlib/utf8.jou, because it
        # uses a bitwise shift (<<=) that the bootstrap transpiler doesn't know.
        mv -v stdlib/utf8.jou stdlib/utf8_old.jou
        echo '
@public
def utf8_encode_char(u: uint32) -> byte*:
    return "jgdspoisajdgpoiasjdgoiasjdpoigdsa"
' > stdlib/utf8.jou

        echo "Converting Jou code to C..."
        "$python" ../../../bootstrap_transpiler.py compiler/main.jou > compiler.c

        echo "Compiling C code..."
        # TODO: -w silences all warnings. We might not want that in the future.
        #
        # TODO: On windows, this needs at least -O2 because the C code is so
        #       bad it overflows the stack if optimizer doesn't reduce stack
        #       usage!!! Instead emit better C code? Or at least handle union
        #       members, or don't create so many local variables? Last time it
        #       failed on creating an AstExpression on stack with all the
        #       different kinds of expressions as struct members instead of
        #       union members...
        #
        #       UPDATE: We hit the Windows stack limit again... and I just
        #       bumped it to 16M instead of the default 1M. Maybe some day I
        #       will clean this up :)
        if [[ "$OS" =~ Windows ]]; then
            $clang -w -O2 compiler.c -o jou_from_c$exe_suffix -Wl,--stack,16777216 ${windows_llvm_files[@]}
        else
            $clang -w -O2 compiler.c -o jou_from_c$exe_suffix $(grep ^link config.jou | cut -d'"' -f2)
        fi

        # Transpiling produces a broken Jou compiler for some reason. I don't
        # know why. But if I recompile once more, it fixes itself.
        mv -v stdlib/utf8_old.jou stdlib/utf8.jou
        echo "Compiling the same Jou code again with the compiler we just made..."
        if [[ "$OS" =~ Windows ]]; then
            # Use correct path to mingw64. This used to copy the mingw64 folder,
            # but it was slow and wasted disk space. Afaik symlinks aren't really a
            # thing on windows.
            JOU_MINGW_DIR=../../../mingw64 ./jou_from_c.exe -o jou.exe compiler/main.jou
        else
            ./jou_from_c -o jou compiler/main.jou
        fi
    )
}

function compile_next_jou_compiler() {
    local previous_numbered_commit=$1
    local numbered_commit=$2

    local previous_folder=tmp/bootstrap_cache/$previous_numbered_commit
    local previous_number=${previous_numbered_commit%%_*}

    local folder=tmp/bootstrap_cache/$numbered_commit
    local number=${numbered_commit%%_*}
    local commit=${numbered_commit#*_}

    echo -e "${CYAN}$0: Using the previous Jou compiler to compile the next Jou compiler.${RESET} ($previous_number --> $number)"

    echo "Checking out commit $number into $folder"
    rm -rf $folder
    mkdir -p $folder
    git archive --format=tar $commit | (cd $folder && tar xf -)
    cp -v config.jou $folder || true

    echo "Copying previous executable..."
    cp -v $previous_folder/jou$exe_suffix $folder/jou_bootstrap$exe_suffix

    if [[ "$OS" =~ Windows ]]; then
        echo "Copying LLVM files..."
        # These files used to be in a separate "libs" folder next to mingw64 folder.
        # Now they are in mingw64/lib.
        # They were also named slightly differently before.
        if [ $number -le 16 ]; then
            mkdir $folder/libs
            for f in ${windows_llvm_files[@]}; do
                cp $f $folder/libs/$(basename -s .dll.a $f).a
            done
        else
            mkdir -p $folder/mingw64/lib
            cp ${windows_llvm_files[@]} $folder/mingw64/lib/
        fi
    fi

    (
        cd $folder

        if [ $number -eq 23 ]; then
            # I changed how the assert statement works: the new compiler imports
            # "stdlib/assert.jou" in every file that uses `assert`, and the old
            # compiler doesn't expect it so it gives a bunch of unused import
            # warnings. Let's get rid of the warnings.
            echo "Deleting stdlib/assert.jou imports..."
            sed -i -e '/import "stdlib\/assert.jou"/d' compiler/*.jou compiler/*/*.jou
        fi

        echo "Deleting version check..."
        sed -i -e "/Found unsupported LLVM version/d" Makefile.*

        if [ $number -le 23 ] && ! [[ "$OS" =~ Windows ]]; then
            echo "Patching Jou code to use or not use aarch64 depending on config..."
            sed -i -e s/'if MACOS:'/'if LLVM_HAS_AARCH64:'/g compiler/target.jou
            sed -i -e s/'if not WINDOWS:'/'if LLVM_HAS_AARCH64:'/g compiler/target.jou
            grep LLVM_HAS_AARCH64 compiler/target.jou  # fail if it replaced nothing
            sed -i -e '1i\
import "../config.jou"'$'\n' compiler/target.jou
        fi

        echo "Running make..."

        # The jou_bootstrap(.exe) file should never be rebuilt.
        # We don't want bootstrap inside bootstrap.
        local make_flags="--old-file jou_bootstrap$exe_suffix"

        if [[ "$OS" =~ Windows ]]; then
            # Use correct path to mingw64. This used to copy the mingw64 folder,
            # but it was slow and wasted disk space. Afaik symlinks aren't really a
            # thing on windows.
            make_flags="$make_flags JOU_MINGW_DIR=../../../mingw64"
        else
            # Also don't rebuild config.jou
            make_flags="$make_flags --old-file config.jou"
        fi

        $make $make_flags jou$exe_suffix
    )
}

# Figure out how far back in history we need to go.
# If nothing exists, use the bootstrap transpiler script.
numbered_commit=
for (( i = ${#numbered_commits[@]}-1 ; i >= 0 ; i-- )) ; do
    if [ -f tmp/bootstrap_cache/${numbered_commits[i]}/jou$exe_suffix ]; then
        numbered_commit=${numbered_commits[i]}
        echo -e "${CYAN}$0: Found a previously built executable for commit ${numbered_commit%%_*} (${numbered_commit#*_}).${RESET}"
        echo "Delete tmp/bootstrap_cache/${numbered_commit} if you want to build it again."
        break
    fi
done

if [ -z "$numbered_commit" ]; then
    transpile_with_python_and_compile
    numbered_commit=$bootstrap_transpiler_numbered_commit
fi

while true; do
    previous_numbered_commit=$numbered_commit

    # Find next commit
    for (( i = 0 ; i < ${#numbered_commits[@]} ; i++ )) ; do
        if [ ${numbered_commits[i]} == $numbered_commit ]; then
            numbered_commit=${numbered_commits[i+1]}
            break
        fi
    done
    if [ -z "$numbered_commit" ]; then
        # Reached end of list
        break
    fi

    compile_next_jou_compiler $previous_numbered_commit $numbered_commit
done

echo -e "${CYAN}$0: Copying the bootstrapped executable...${RESET}"
cp -v tmp/bootstrap_cache/$previous_numbered_commit/jou$exe_suffix ./jou_bootstrap$exe_suffix
echo -e "${CYAN}$0: Done!${RESET}"
