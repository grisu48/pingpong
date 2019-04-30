default: all
all: echo eping

echo:	echo.c
	gcc -Wall -Werror -pedantic -std=c99 -o $@ $< -pthread
eping:	eping.c
	gcc -Wall -Werror -pedantic -std=c99 -o $@ $< -D_DEFAULT_SOURCE -D_BSD_SOURCE -lm
