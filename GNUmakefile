# This file tells GNU make to use the correct makefile. GNU make finds
# this before Makefile.
#
# We use GNU make on Windows, Linux and MacOS, because it is the most
# commonly available make on those platforms.
#
# This can't be named Makefile, because then BSD make would find it, but
# we need GNU make's if/else syntax that doesn't work in BSD make.

ifneq (,$(findstring Windows,$(OS)))
	include Makefile.windows
else
	include Makefile.posix
endif
