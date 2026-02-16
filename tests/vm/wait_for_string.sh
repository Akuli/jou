#!/bin/bash
#
# "./wait_for_string.sh foo somecommand --bar" runs "somecommand --bar" and
# kills it when it prints anything that contains "foo". The string "foo" can
# appear as a substring anywhere in the output, and killing happens immediately
# as soon as the command writes it to its standard output. Output of the
# command is shown as usual in addition to the substring searching.
#
# For example, this logs in as root to serial console on localhost:4444, and
# waits until the shell prompt appears:
#
#   $ echo root | ./wait_for_string.sh "root@localhost:~#" ncat --no-shutdown localhost 4444
#
# Here is another example, which is more useful for developing this script. It
# tells Python to print "blah" and "foobar", with 1 second delays before and
# after the "foo" part of "foobar". The `-u` makes Python's print unbuffered.
# You should never see "bar", and the command should stop immediately after
# printing "foo".
#
#   $ ./wait_for_string.sh foo python3 -u -c 'from time import sleep; print("blah"); sleep(1); print("foo",end=""); sleep(1); print("bar")'
#
# You can try the same with bash instead of Python, but note that the "sleep"
# subprocess actually continues running because this script only kills bash:
#
#   $ ./wait_for_string.sh foo bash -c 'echo blah; sleep 1; echo -n foo; sleep 1; echo bar'
#
# The following example demonstrates how slow this script is (it is pretty
# slow, but that doesn't matter for our use cases):
#
#   $ ./wait_for_string.sh hi base64 /dev/urandom
#
# Implementing this is more difficult than you would expect, because many tools
# (sed, grep, awk, ...) don't process any input until a newline character
# appears, and forcing them to process byte by byte instead of line by line is
# not really a thing.

set -e -o pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <string> <command_and_args>"
    exit 2
fi
string="$1"
shift

pid_of_this_script=$$

statusfile="$(mktemp)"
trap 'rm "$statusfile"' EXIT

"$@" | (
    buf=""
    while IFS= read -r -N1 -d '' byte; do
        # Show output as usual
        printf '%s' "$byte"

        # Check for the substring
        buf+="$byte"
        if [[ "$buf" == *"$string" ]]; then
            # Found substring. Let's find the command we called and kill it.
            for proc in $(pgrep -P $pid_of_this_script); do
                # This output handling thing runs in a new process and we don't
                # want it to kill itself.
                #
                # For some reason $$ doesn't work here but $BASHPID works.
                if [ $proc != $BASHPID ]; then
                    echo killed > "$statusfile"
                    kill $proc
                fi
            done
            break
        fi

        # Do not allow buf to become too long
        if [ ${#buf} -gt 1000 ]; then
            buf=${buf:500}
        fi
    done
# Fail if the command fails, except in the case were we killed it. Then we
# ignore the "failure" (killed) status.
) || [ "$(cat "$statusfile")" == killed ]
