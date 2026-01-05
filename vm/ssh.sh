#!/bin/bash

set -e -o pipefail

if [[ "$1" == -* ]]; then
    echo "Usage:"
    echo "  $0              # Interactive shell"
    echo "  $0 git status   # Run a command inside VM (git status is just an example)"
    echo ""
    echo "You need to start a VM first before using this script."
    exit 2
fi

if ! [ -f "$(dirname "$0")/key" ]; then
    echo "$0: ssh key hasn't been created yet" >&2
    exit 1
fi

# The flags we pass to ssh basically disable a bunch of checks that make it
# secure. We don't care, because we're connecting to localhost.
#
#   -p
#       Port number that all VMs use.
#
#   -i ...
#       Our private ssh key. See keygen.sh.
#
#   -o UserKnownHostsFile=/dev/null
#       Do not create a list of computers where we have connected in the
#       past. This makes ssh think that we're always connecting for the
#       first time.
#
#       ssh shows a scary (and in this case useless) error message every
#       time root@localhost points to a different VM than last time. If you
#       connect over the internet, that would be a man in the middle attack:
#       you would be connecting to a hacker's computer instead of your own
#       computer. But in our case, we want root@localhost to be whatever VM
#       is running.
#
#   -o StrictHostKeyChecking=no
#       Don't ask any questions when connecting for the "first" time (in
#       our case, every time). Instead, assume that you connected to the
#       right place, not a man in the middle attack. Usually ssh would
#       still detect future man in the middle attacks.
#
#       Because this is not perfectly secure, ssh still shows a one-line
#       warning message.
#
#   -o LogLevel=ERROR:
#       This silences the warning when connecting for the "first" time.
#
#       Unfortunately this silences all warnings. It doesn't really matter
#       as much as I thought: almost all ssh problems you could have here
#       are errors, not warnings.
#
exec ssh root@localhost \
    -p 2222 \
    -i "$(dirname "$0")/key" \
    -o UserKnownHostsFile=/dev/null \
    -o StrictHostKeyChecking=no \
    -o LogLevel=ERROR \
    "$@"
