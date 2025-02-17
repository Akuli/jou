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
)

for commit in ${commits[@]}; do
    if ! (git log --format='%H' || true) | grep -qx $commit; then
        echo "Error: commit $commit not in history of current branch"
        exit 1
    fi
done

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
    local commit=${commits[$i]}
    echo tmp/bootstrap_cache/$(printf '%03d' $((i+1)))_$commit
}

# Figure out how far back in history we need to go
cached_idx=$((${#commits[@]} - 1))
while [ $cached_idx != -1 ]; do
    folder=$(folder_of_commit $cached_idx)
    if [ -f $folder/jou$exe_suffix ]; then
        show_message "Using cached executable for commit $((cached_idx+1))/${#commits[@]}"
        echo "Delete $folder if you want to build it again."
        break
    else
        ((cached_idx--)) || true
    fi
done

for i in ${!commits[@]}; do
    if [ $i -le $cached_idx ]; then
        continue
    fi

    commit=${commits[$i]}
    show_message "Checking out and compiling commit ${commit:0:10} ($((i+1))/${#commits[@]})"

    folder=$(folder_of_commit $i)
    rm -rf $folder
    mkdir -vp $folder

    # This seems to be the best way to checkout a commit into a folder.
    git archive --format=tar $commit | (cd $folder && tar xf -)

    if [[ "$OS" =~ Windows ]]; then
        cp -r libs mingw64 $folder
    fi

    if [[ "$OS" =~ "Windows" ]] && [ $i == 0 ]; then
        # The compiler written in C needed LLVM headers, and getting them on
        # Windows turned out to be more difficult than expected, so I included
        # them in the repository as a zip file.
        echo "Extracting LLVM headers..."
        (cd $folder && unzip -q llvm_headers.zip)
    fi

    if [ $i != 0 ]; then
        cp $(folder_of_commit $((i-1)))/jou$exe_suffix $folder/jou_bootstrap$exe_suffix
        # Convince make that jou_bootstrap(.exe) is usable as is, and does
        # not need to be recompiled. We don't want bootstrap inside bootstrap.
        touch $folder/jou_bootstrap$exe_suffix
    fi

    (cd $folder && $make jou$exe_suffix)
done

show_message "Copying the bootstrapped executable"
cp -v $(folder_of_commit $((${#commits[@]} - 1)))/jou$exe_suffix ./jou_bootstrap$exe_suffix

show_message "Done!"
