#!/bin/bash
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
)

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
    echo -e "\x1b[36m====== $0: $1 ======\x1b[0m"
}

show_message "Creating temporary working directory"
mkdir -vp tmp
rm -rf tmp/bootstrap
git clone . tmp/bootstrap
if [[ "$OS" =~ Windows ]]; then
    cp -r libs llvm llvm-c mingw64 tmp/bootstrap
fi
cd tmp/bootstrap

for i in ${!commits[@]}; do
    commit=${commits[$i]}
    show_message "Checking out and compiling commit ${commit:0:10} ($((i+1))/${#commits[@]})"

    git checkout -q $commit
    $make jou$exe_suffix
    mv -v jou$exe_suffix jou_bootstrap$exe_suffix
    $make clean
done

cd ../..  # go out of tmp/bootstrap
cp -v tmp/bootstrap/jou_bootstrap$exe_suffix .

show_message "Bootstrapping done, you now have an executable ./jou_bootstrap$exe_suffix"
