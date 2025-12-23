# Virtual Machines

This directory contains scripts to set up virtual machines for testing Jou on
different platforms. When you run the scripts, the virtual machines are
created here.

The scripts assume that the host computer (that is, your actual computer or
GitHub Actions) is a reasonably modern linux distro on x86_64.

Here are some guidelines for working on the scripts:
- Scripts should work on GitHub Actions and locally. This way they are easy to
  develop and debug.
- Make the scripts as self-contained as possible. The same script should
  download an operating system, create a VM, set up the operating system in the
  VM, copy Jou into the VM and run a command inside the VM.
- The command-line usage of each script should be similar to
  `./netbsd.sh amd64 git status`: first command-line argument is the CPU
  architecture, and the rest is a command to run inside the VM. If the script
  supports just one architecture, you can just fail with an error if anything
  else is specified.
- Scripts should be able to continue from intermediate states when that can be
  implemented with a reasonable amount of effort. However, don't go to the
  other extreme and over-engineer this; just make sure that the developer
  experience is good. For example:
    - Don't create a new VM if it already exists.
    - Don't delete the VM when the script finishes.
    - Don't download things again if already downloaded (partially or fully).
    - Don't restart the VM if it's already running.
    - Don't stop the VM when your script fails with an error, so that after
      fixing the error, you don't need to wait for it to start again.
    - Don't bother with lock files, backup copies or state snapshots. If a VM
      is broken, it is (by design) very easy to just delete the VM and re-run
      the script again in exactly the same way.
- Do not invoke `sudo` inside the scripts except when the script is running on
  GitHub Actions. Use the proper permissions for local development. For
  example, add yourself to the `kvm` group if you want to use kvm.
- Please checksum downloaded files with sha256. This minimizes Jou's attack
  surface: compromising just one of the things we download shouldn't be enough
  to get access to a Jou developer's computer. Please don't use sha512, because
  sha512 hashes are ridiculously long, and not really meaningfully more secure
  than sha256.
- Place the VMs into subdirectories, so that Jou developers can have multiple
  VMs at the same time without conflicts. All subdirectories are gitignored.
  Make sure that everything goes in the subdirectory, so that a simple
  `rm -rf vm/name_of_subdirectory` fully deletes the VM.
- Use `cd "$(dirname "$0")"` or similar so that scripts "just work" regardless
  of current working directory. This is convenient when you work on something
  and you want to check whether something works inside a VM.
- Use ssh for as much as you can. This way output of commands is always shown
  correctly, and test failures (nonzero exit code) are noticed.
- Use `set -e -o pipefail`. If this gets in your way, use `|| true` only in the
  places that need it. This way failures are not silenced except where you
  expect to get failures.
- Use the [`wait_for_string.sh`](./wait_for_string.sh) script instead of the
  `expect` program, for two reasons:
    - Jou developers already need to know at least 4 programming languages, and
      I don't really want to add Tcl to the mix.
    - `expect` would be an unnecessary dependency.

Dependencies (in addition to what you need for Jou anyway):

```
$ sudo apt install openssh-client qemu-system-x86
```
