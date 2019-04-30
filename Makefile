default: all
all: echo udp_ping tcp_ping

echo:	echo.c
	gcc -Wall -Werror -pedantic -std=c99 -o $@ $< -pthread
udp_ping:	udp_ping.c
	gcc -Wall -Werror -pedantic -std=c99 -o $@ $< -D_DEFAULT_SOURCE -D_BSD_SOURCE -lm
tcp_ping:	tcp_ping.c
	gcc -Wall -Werror -pedantic -std=c99 -o $@ $< -D_DEFAULT_SOURCE -D_BSD_SOURCE -lm
