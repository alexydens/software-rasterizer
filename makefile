CFLAGS = -std=c99 -pedantic -Wall -Wextra -O3
LDFLAGS = -Wl,--copy-dt-needed-entries -lSDL2 -lOAK

build/main: src/main.c
	gcc $(CFLAGS) src/*.c -o build/main $(LDFLAGS)

clean:
	rm -rf build/*

run: build/main
	./build/main
