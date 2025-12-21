# Virtual Machines

This folder contains scripts to set up virtual machines for testing Jou on
different platforms. When you run the scripts, the virtual machines are
created here.

The scripts assume that the host computer (that is, your actual computer or
GitHub Actions) is a reasonably modern linux distro.

Here are some guidelines for working on the scripts:
- Scripts should work on GitHub Actions and locally. This way they are easy to
  develop and debug.
- Make the scripts as self-contained as possible. A simple `./script.sh` should
  download the operating system, set up the VM, copy Jou into the VM and run
  the tests.
- Scripts should be able to continue from an intermediate state. For example,
  don't download things again if already downloaded, and don't restart the VM
  if it's already running.
- Please checksum everything you download with sha256. This minimizes attack
  vectors: compromising just any one of the things we download shouldn't
  be enough to get access to a Jou developer's computer. But please don't use
  sha512, because sha512 hashes are ridiculously long and not meaningfully more
  secure than sha256.
- Use ssh for as much as you can. This way output of commands is always shown
  correctly, and test failures (nonzero exit code) are noticed.
- Use `set -e -o pipefail`. If this gets in your way, use `|| true` only in
  the places that need it. This way failures are not silenced except where you
  expect to get failures.
- Do not use the `expect` program. While it works for this kind of thing, it
  is an unnecessary dependency.
