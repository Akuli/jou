#!/bin/bash

set -e -o pipefail

#if git status --porcelain --untracked=no | grep -q .; then
#    echo "You have uncommitted changes in Git. Refusing to bootstrap." >&2
#    exit 1
#fi
#
## Go back to current branch when this script exits, even if failure
#original_branch="$(git rev-parse --abbrev-ref HEAD)"
#if [ "$original_branch" == "HEAD" ]; then
#    original_branch="$(git rev-parse HEAD)"
#fi
#
if [[ "${OS:=$(uname)}" =~ Windows ]]; then
    source activate
    make=mingw32-make
    exe_suffix=.exe
elif [[ "$OS" =~ NetBSD ]]; then
    make=gmake
    exe_suffix=
else
    make=make
    exe_suffix=
fi
#
#$make clean
#trap '($make clean >/dev/null || true) && git checkout $original_branch' EXIT

# Create a separate working directory to avoid touching user's files.
rm -rf bootstrap
git clone . bootstrap
cd bootstrap

commits=(
    5a9722ab4235fd9b081613dd37c9666d2193413f  # last commit on main that contains the compiler written in C
    7f6367c7a1ef89a723e165f41321da1a394fb048  # TODO: this is a test, remove
    20c1ce021d7d7ac69b715de315149b1c25fec208  # TODO: this is a test, remove
)

for i in ${!commits[@]}; do
    commit=${commits[$i]}
    printf "\n\n====== Checking out commit %s and compiling it... ======\n\n" ${commit:0:10}
    git checkout -q $commit
    $make jou$exe_suffix
    mv -v jou$exe_suffix jou_bootstrap$exe_suffix
    $make clean
done

cd ..
cp -v bootstrap/jou_bootstrap$exe_suffix .

echo "

Bootstrapping done, you now have an executable ./jou_bootstrap$exe_suffix" 
