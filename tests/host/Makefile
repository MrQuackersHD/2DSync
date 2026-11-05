CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Wno-format-truncation -O2 -I../../common
SRC := ../../common/sha256.c ../../common/conflict.c ../../common/title_filter.c ../../common/net_policy.c ../../common/container.c ../../common/icon_tiles.c test_main.c

.PHONY: test clean
test:
	$(CC) $(CFLAGS) -o test_host $(SRC)
	./test_host

clean:
	rm -f test_host
