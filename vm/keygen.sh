#!/bin/bash
#
# This script generates ssh keys if they don't exist yet.
#
# We use the same key for all VMs, but not for anything else. So you don't need
# to worry about accidentally pushing your "private" key to Git.
#
# The key is not committed to git, because putting generated files to Git can
# look suspicious (see xz-utils hack). But that is the only reason.

set -e -o pipefail

if [ $# != 0 ]; then
    echo "This script takes no arguments." >&2
    exit 2
fi

cd "$(dirname "$0")"

if ! [ -f key ] || ! [ -f key.pub ]; then
    # Key not generated yet.
    # Use a fast algorithm instead of RSA (very slow) even if RSA is default.
    rm -f key key.pub  # in case one part of the key exists and one doesn't
    (yes || true) | ssh-keygen -q -t ed25519 -f key -N ''
fi

# Print public key so that a simple $(../keygen.sh) does everything you need
# when setting up ssh.
cat key.pub
