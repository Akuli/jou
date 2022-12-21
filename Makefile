SRC := $(wildcard src/*.c)
CFLAGS += -Wall -Wextra -Wpedantic -std=c11
CFLAGS += -g

obj/%.o: src/%.c $(wildcard src/*.h)
	mkdir -vp obj && $(CC) -c $(CFLAGS) $< -o $@ $(LDFLAGS)

all: newlangc
newlangc: $(SRC:src/%.c=obj/%.o)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -rvf obj newlangc
