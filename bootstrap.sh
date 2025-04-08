#!/usr/bin/env bash
#
# The Jou compiler is written in Jou, but it doesn't help you very much if you
# have nothing that can compile or run Jou code. That's why this script exists.
#
# This script takes old versions of Jou from Git history, starting with the
# last commit that came with a compiler written in C. The previous version of
# Jou is always used to compile the next.
#
# We don't take all commits from Git history, because compiling all of them
# would be slow. Instead, this script contains a hard-coded list. You need to
# add a commit to the end of the list before you can use a new language feature
# in the Jou compiler or standard library used by the compiler.

set -e -o pipefail

# Add the latest commit on main branch to the end of the list if this script
# produces a compiler that is too old.
commits=(
    1c7ce74933aea8a8862fd1d4409735b9fb7a1d7e  # last commit on main that contains the compiler written in C
    b339b1b300ba98a2245b493a58dd7fab4c465020  # "match ... with ..." syntax
    874d1978044a080173fcdcc4e92736136c97dd61  # "match some_integer:" support
    8a0bb4b50ef77932243f4f65ae90be84e763ec45  # evaluating "if WINDOWS:" and such inside functions
    2b33658b17239c68c84b034c33c2a7c145da43dd  # bidirectional type inference for arrays of strings
    adad3da193f8bb5473c3d63602997ab70a3571cb  # @public can be applied to classes, enums and global variables
    2c2e8efa3be20fb63727d96731b92ec515b872db  # "pass" in class, "if False" at compile time
    d8c1f36e92812185a999f87a1fadc7e9eae78bd0  # the "link" keyword
    ec92977acf51c265c28eefdab36d7ef272ac9eda  # the "const" keyword
    6d443fa1da12d462fe6f6a64b75ce74227bc13ff  # "const" keyword works with strings
    09fa4d1b9d414d8a05b4e45b316fad043aaf5ad7  # generic classes
    519539bfc5551a6e6b9c3fa3070156b07a534601  # generic class bug fix
    30bf61efa40832a2429eee34c8cc93e80ea7f591  # @inline
    722d066c840bf9c07dafd69e5bd1d12823f04b25  # bug fixes for using @inline in generic classes
    e7fea1f2ed602a7c191f8c8c605fa56ae2468723  # JOU_MINGW_DIR environment variable
    212db69885bd1f18e7fa67458110b81b3b1cd812  # "./windows_setup.sh --small" starts from this commit
)

for commit in ${commits[@]}; do
    if ! (git log --format='%H' || true) | grep -qx $commit; then
        echo "Error: commit $commit not in history of current branch"
        exit 1
    fi
done

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
elif [[ "$OS" =~ NetBSD ]]; then
    make="gmake"
    exe_suffix=""
else
    make="make"
    exe_suffix=""
fi

function show_message {
    echo -e "\x1b[36m===== $0: $1 =====\x1b[0m"
}

function folder_of_commit {
    local i=$1
    local commit=${commits[i-1]}
    echo tmp/bootstrap_cache/$(printf '%03d' $i)_$commit
}

# Figure out how far back in history we need to go.
# 0 means full rebuild, 1 means skip first commit, 2 means skip two commits etc.
skip_count=${#commits[@]}
while [ $skip_count != 0 ]; do
    folder=$(folder_of_commit $skip_count)
    if [ -f $folder/jou$exe_suffix ]; then
        show_message "Using cached executable for commit $skip_count/${#commits[@]}"
        echo "Delete $folder if you want to build it again."
        break
    else
        ((skip_count--)) || true
    fi
done

for i in $(seq 1 ${#commits[@]}); do
    if [ $i -le $skip_count ]; then
        continue
    fi

    commit=${commits[i-1]}
    show_message "Checking out and compiling commit ${commit:0:10} ($i/${#commits[@]})"

    folder=$(folder_of_commit $i)
    rm -rf $folder
    mkdir -p $folder

    # This seems to be the best way to checkout a commit into a folder.
    git archive --format=tar $commit | (cd $folder && tar xf -)

    (
        cd $folder

        if [ $i == 1 ]; then
            echo "Patching C code..."
            # Delete optimizations from C code. They cause linker errors with new LLVM versions.
            # Passing in -O0 doesn't help, because the optimizing code would still be linked.
            # See https://blog.llvm.org/posts/2021-03-26-the-new-pass-manager/

            # Delete old include
            sed -i -e '/#include .*PassManagerBuilder.h/d' bootstrap_compiler/main.c

            # Delete old optimize function
            sed -i -e '/static void optimize/,/^}/d' bootstrap_compiler/main.c

            # Add new optimize function
            sed -i -e '1i\
static void optimize(void *module, int level) { (void)module; (void)level; }'$'\n' bootstrap_compiler/main.c
        fi

        if [ $i -le 7 ]; then
            echo "Deleting version check..."
            # Delete version checks to support bootstrapping on newer LLVM versions
            sed -i -e "/Found unsupported LLVM version/d" Makefile.*

            echo "Patching Jou code..."
            # Delete function calls we no longer need
            sed -i -e /LLVMCreatePassManager/d compiler/main.jou
            sed -i -e /LLVMDisposePassManager/d compiler/main.jou
            sed -i -e /LLVMPassManagerBuilderSetOptLevel/d compiler/main.jou
            sed -i -e /LLVMRunPassManager/d compiler/main.jou

            # Old code creates and destroys LLVMPassManagerBuilder just like new code
            # creates and destroys LLVMPassBuilderOptions.
            sed -i -e s/LLVMPassManagerBuilderCreate/LLVMCreatePassBuilderOptions/g compiler/*.jou
            sed -i -e s/LLVMPassManagerBuilderDispose/LLVMDisposePassBuilderOptions/g compiler/*.jou

            # Change the function that does the optimizing: LLVMPassManagerBuilderPopulateModulePassManager --> LLVMRunPasses
            sed -i -e s/'LLVMPassManagerBuilderPopulateModulePassManager.*'/'LLVMRunPasses(module, "default<O1>", target.target_machine, pmbuilder)'/g compiler/main.jou
            sed -i -e s/'declare LLVMPassManagerBuilderPopulateModulePassManager.*'/'declare LLVMRunPasses(a:void*, b:void*, c:void*, d:void*) -> void*'/g compiler/llvm.jou
        fi

        if [[ "$OS" =~ Windows ]] && [ $i -le 14 ]; then
            # Old version of Jou. Doesn't support the JOU_MINGW_DIR environment variable.
            # Patch code to find mingw64 in the directory where it is.
            echo "Patching to specify location of mingw64..."
            sed -i 's/mingw64/..\\\\..\\\\..\\\\mingw64/g' compiler/run.jou
            if [ $i == 1 ]; then
                sed -i 's/mingw64/..\\\\..\\\\..\\\\mingw64/g' bootstrap_compiler/output.c
            fi
        fi
    )

    if [[ "$OS" =~ Windows ]] && [ $i -le 15 ]; then
        # These files used to be in a separate "libs" folder next to mingw64 folder.
        # Now they are in mingw64/lib.
        # They were also named slightly differently.
        echo "Copying files..."
        mkdir $folder/libs
        for file in mingw64/lib/libLLVM*.dll.a mingw64/lib/libLTO.dll.a; do
            cp -v $file $folder/libs/${file/.dll/}
        done
    fi

    if [[ "$OS" =~ Windows ]] && [ $i == 1 ]; then
        # The compiler written in C needed LLVM headers, and getting them on
        # Windows turned out to be more difficult than expected, so I included
        # them in the repository as a zip file.
        echo "Extracting LLVM headers..."
        (cd $folder && unzip -q llvm_headers.zip)
    fi

    echo "Copying previous bootstrap compiler..."
    if [ $i != 1 ]; then
        cp $(folder_of_commit $((i-1)))/jou$exe_suffix $folder/jou_bootstrap$exe_suffix
    fi

    echo "Running make..."

    # The jou_bootstrap(.exe) file should never be rebuilt.
    # We don't want bootstrap inside bootstrap.
    make_flags="--old-file jou_bootstrap$exe_suffix"

    if [[ "$OS" =~ Windows ]]; then
        # Use correct path to mingw64. This used to copy the mingw64 folder,
        # but it was slow and wasted disk space. Afaik symlinks aren't really a
        # thing on windows.
        make_flags="$make_flags JOU_MINGW_DIR=../../../mingw64"
        if [ $i == 1 ]; then
            make_flags="$make_flags CC=../../../mingw64/bin/clang.exe"
        fi
    fi

    (cd $folder && $make $make_flags jou$exe_suffix)
done

show_message "Copying the bootstrapped executable"
cp -v $(folder_of_commit ${#commits[@]})/jou$exe_suffix ./jou_bootstrap$exe_suffix

show_message "Done!"
