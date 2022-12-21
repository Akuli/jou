$ make -j2 && ./newlangc hello.nl


Checking for memory leaks with valgrind:

$ make -j2 && valgrind --leak-check=full --show-leak-kinds=all ./newlangc hello.nl
