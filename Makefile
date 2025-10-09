ifneq (,$(findstring Windows,$(OS)))
	include Makefile.windows
else
	include Makefile.posix
endif

.PHONY: clean
clean:
	rm -rvf *.exe config.jou jou jou_bootstrap tmp
	find . -name jou_compiled -print -exec rm -rf '{}' +
