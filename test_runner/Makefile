.PHONY: run
run: toml
	./toml-test-* test -decoder ./toml

toml: toml.jou
	jou -o toml toml.jou
