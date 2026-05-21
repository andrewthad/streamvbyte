# minimalist makefile
.SUFFIXES:
#
.SUFFIXES: .cpp .o .c .h

PROCESSOR:=$(shell uname -m)


CFLAGS = -fvisibility=hidden -mbmi2 -mavx2 -msse4 -std=c99 -Os -Wall -Wextra -pedantic -Wshadow -g
LDFLAGS = -shared
LNLIBNAME=libstreamvbyte.so
all: unit
test:
	./unit
dyntest:   dynunit $(LNLIBNAME)
	LD_LIBRARY_PATH=. ./dynunit

HEADERS=./include/streamvbyte.h ./include/streamvbyte_zigzag.h

uninstall:
	for h in $(HEADERS) ; do rm  /usr/local/$$h; done
	rm /usr/local/lib/libstreamvbyte.so
	ldconfig

OBJECTS= streamvbyte_decode.o streamvbyte_encode.o streamvbyte_zigzag.o

streamvbyte_zigzag.o: ./src/streamvbyte_zigzag.c $(HEADERS)
	$(CC) $(CFLAGS) -c ./src/streamvbyte_zigzag.c -Iinclude

streamvbyte_decode.o: ./src/streamvbyte_decode.c $(HEADERS)
	$(CC) $(CFLAGS) -c ./src/streamvbyte_decode.c -Iinclude

streamvbyte_encode.o: ./src/streamvbyte_encode.c $(HEADERS)
	$(CC) $(CFLAGS) -c ./src/streamvbyte_encode.c -Iinclude

shuffle_tables: ./utils/shuffle_tables.c
	$(CC) $(CFLAGS) -o shuffle_tables ./utils/shuffle_tables.c



example: ./examples/example.c    $(HEADERS) $(OBJECTS)
	$(CC) $(CFLAGS) -o example ./examples/example.c -Iinclude  $(OBJECTS)

compress_decimal_encoded_integers: ./examples/compress_decimal_encoded_integers.c    $(HEADERS) $(OBJECTS)
	$(CC) $(CFLAGS) -o compress_decimal_encoded_integers ./examples/compress_decimal_encoded_integers.c -Iinclude  $(OBJECTS)

perf: ./tests/perf.c    $(HEADERS) $(OBJECTS)
	$(CC) $(CFLAGS) -o perf ./tests/perf.c -Iinclude  $(OBJECTS) -lm


writeseq: ./tests/writeseq.c    $(HEADERS) $(OBJECTS)
	$(CC) $(CFLAGS) -o writeseq ./tests/writeseq.c -Iinclude  $(OBJECTS)

unit: ./tests/unit.c    $(HEADERS) $(OBJECTS)
	$(CC) $(CFLAGS) -o unit ./tests/unit.c -Iinclude -Isrc  $(OBJECTS)

clean:
	rm -f unit *.o $(LNLIBNAME)  example shuffle_tables perf writeseq dynunit
