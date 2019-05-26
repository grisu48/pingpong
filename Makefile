CC=gcc
CC_FLAGS=-Og -g2 -Wall -Wextra -Werror -std=c99


default: bw
legacy: echod udp_ping tcp_ping latency throughput

echod:	echod.c
	$(CC) $(CC_FLAGS) -o $@ $< -pthread
udp_ping:	udp_ping.c
	$(CC) $(CC_FLAGS) -o $@ $< -D_DEFAULT_SOURCE -D_BSD_SOURCE -lm
tcp_ping:	tcp_ping.c
	$(CC) $(CC_FLAGS) -o $@ $< -D_DEFAULT_SOURCE -D_BSD_SOURCE -lm
latency:	latency.c
	$(CC) $(CC_FLAGS) -o $@ $< -D_DEFAULT_SOURCE -D_BSD_SOURCE -lm
throughput:	throughput.c
	$(CC) $(CC_FLAGS) -o $@ $< -D_DEFAULT_SOURCE -D_BSD_SOURCE -lm
bw:	bw.c
	$(CC) $(CC_FLAGS) -o $@ $< -D_DEFAULT_SOURCE -D_BSD_SOURCE -lm -pthread

install:	bw
	install bw /usr/local/bin
