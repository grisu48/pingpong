/* =============================================================================
 * 
 * Title:         Simple network bandwidth test program
 * Author:        Felix Niederwanger
 * License:       Copyright (c), 2019 Felix Niederwanger
 *                MIT license (http://opensource.org/licenses/MIT)
 * 
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h> 
#include <sys/time.h>
#include <netinet/tcp.h>

#define BUF_SIZE 102400		// Make sure it's larger than the MTU
#define SERIES 10			// Number of iterations per size

static volatile int sock = 0;
static volatile size_t bytes_total;		// Bytes counter
static int warmup_s = 0;				// Warmup seconds

int run_server(const int port);
int run_client(const char* remote, const int port);

void cleanup() {
	if(sock > 0)
		close(sock);
	sock = 0;
}

int main(int argc, char** argv) {
	bool server = false;
	int port = 12998;
	char* remote = "127.0.0.1";
	
	if(argc < 2) {
		printf("Usage: %s [OPTIONS] REMOTE [PORT]\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	for(int i=1;i<argc;i++) {
		const char* arg = argv[i];
		if(strlen(arg) == 0) continue;
		if(arg[0] == '-') {
			if(!strcmp("-h", arg) || !strcmp("--help", arg)) {
				printf("Stupid simple network bandwidth test program\n");
				printf("  2019 Felix Niederwanger\n");
				printf("\n");
				printf("Usage: %s [OPTIONS] REMOTE [PORT]\n", argv[0]);
				printf("OPTIONS\n");
				printf("  -h, --help                 Print this help message\n");
				printf("  -s, --server               Run as server\n");
				printf("      --warmup SECONDS       Run benchmark after a given warmup delay\n");
				printf("\n");
				printf("https://github.com/grisu48/pingpong\n");
				exit(EXIT_SUCCESS);
			} else if(!strcmp("-s", arg) || !strcmp("--server", arg)) {
				server = true;
			} else if(!strcmp("--warmup", arg)) {
				if(i >= argc-1) {
					fprintf(stderr, "Missing time for warmup\n");
					exit(EXIT_FAILURE);
				}
				warmup_s = atoi(argv[++i]);
			} else {
				fprintf(stderr, "Illegal argument: %s\n", arg);
				printf("Type %s --help if you need help\n", argv[0]);
				exit(EXIT_FAILURE);
			}
		} else {
			static bool remote_assign = true;
			if(remote_assign) {
				remote = (char*)arg;
				remote_assign = false;
			}
			else
				port = atoi(arg);
		}
	}
	
	atexit(cleanup);
	int rc = 0;
	if(server) {
		rc = run_server(port);
	} else {
		printf("%s:%d\n", remote, port);
		rc = run_client(remote, port);
	}
	if(rc != 0)
		exit(EXIT_FAILURE);
    exit(EXIT_SUCCESS);
}


typedef struct {
	int sock;
} s_thread_params_t;

void * tcp_client(void * args) {
	// Make parameters thread-local and free memory
	s_thread_params_t *params = (s_thread_params_t*)args;
	const int sock = params->sock;
	free(params);

	// Disable Nagle's algorithm
	int one = 1;
	if(setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(int)) < 0)
		fprintf(stderr, "Warning: Failed to set TCP_NODELAY for new socket: %s\n", strerror(errno));

	size_t received = 0L;
	while(true) {
		// First receive size of packet
		char msg[9] = {'\0'};
		ssize_t l_recv = recv(sock, msg, 8, MSG_WAITALL);
		if(l_recv < 0) {
			fprintf(stderr, "recv failed: %s\n", strerror(errno));
			break;
		} else if(l_recv == 0) {
			break;
		} else if(l_recv < 8) {
			fprintf(stderr, "Incomplete header\n");
			break;
		}
		msg[8] = '\0';
		if(!strcmp("CLOSE", msg)) {
			break;
		} else {
			long size = atol(msg);
			//printf("Receiving %ld bytes ... \n", size);

			// Allocate buffer before receiving!
			char* buf = malloc(sizeof(char)*size);
			if(buf == NULL) {
				fprintf(stderr, "malloc failed: %s\n", strerror(errno));
				sprintf(msg, "ERR     ");
				send(sock, msg, 8, 0);
				break;
			}
			bzero(buf, sizeof(char)*size);
			sprintf(msg, "OK      ");
			if(send(sock, msg, 8, MSG_DONTWAIT) < 0) {
				fprintf(stderr, "send failed: %s\n", strerror(errno));
				break;
			}

			ssize_t len = recv(sock, buf, (size_t)size, MSG_WAITALL);
			if(len < 0) {
				fprintf(stderr, "recv failed: %s\n", strerror(errno));
				break;
			} else if(len < size) {
				fprintf(stderr, "Incomplete recv: %s\n", strerror(errno));
			}
			// Back to the sender
			len = send(sock, buf, (size_t)size, 0);
			if(len < 0) {
				fprintf(stderr, "send_bw failed: %s\n", strerror(errno));
				break;
			}
			received += (size_t)size;
			free(buf);
		}
	}

	//printf("Transferred %ld (x2) bytes\n", received);
	return NULL;
}

int run_server(const int port) {
	int sock = 0;
	
	
    struct sockaddr_in addr; 
    memset(&addr, 0, sizeof(addr)); 
    addr.sin_family = AF_INET; 
    addr.sin_port = htons(port); 
    addr.sin_addr.s_addr = INADDR_ANY; 
    
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
    	fprintf(stderr, "Error creating socket: %s\n", strerror(errno));
    	return -1;
    }
    
    int rc = bind(sock, (const struct sockaddr*)&addr, sizeof(addr));
    if(rc < 0) {
    	fprintf(stderr, "Binding socket failed: %s\n", strerror(errno));
    	close(sock);
    	return -1;
    }
    
    rc = listen(sock, 5);
	if(rc < 0) {
		fprintf(stderr, "Listening failed: %s\n", strerror(errno));
    	close(sock);
    	return -1;
	}

	// Run while socket is opened
	while(sock > 0) {
		struct sockaddr client_addr;
		socklen_t addrlen = sizeof(client_addr);
		const int fd = accept(sock, &client_addr, &addrlen);

		pthread_t tid;
		s_thread_params_t *params = (s_thread_params_t*)malloc(sizeof(s_thread_params_t));
		if(params == NULL) {
			fprintf(stderr, "out of memory");
			return -1;
		}
		params->sock = fd;
		int rc = pthread_create(&tid, NULL, tcp_client, params);
		if(rc < 0) {
			free(params);
			fprintf(stderr, "error creating tcp client thread: %s\n", strerror(errno));
			return -1;
		}
		pthread_detach(tid);
	}

    close(sock);
    return 0;
}

typedef struct {
	long f;
	long s;
} pair_l;

typedef struct {
	long avg;
	long min;
	long max;
} stats_l;

static stats_l stats(const long* arr, const size_t n) {
	stats_l ret;
	ret.avg = 0;
	ret.min = 0;
	ret.max = 0;
	if(n < 1) return ret;
	ret.avg = 0;
	ret.min = arr[0];
	ret.max = arr[0];

	for(size_t i=0;i<n;i++) {
		const long v = arr[i];
		if(v < ret.min) ret.min = v;
		if(v > ret.max) ret.max = v;
		ret.avg += v;
	}
	ret.avg /= n;


	return ret;
}

/** Perform a bandwith test on the given socket by sending the given amout of bytes */
pair_l bw_test(const int sock, const size_t size) {
	pair_l ret;
	ret.f = -1L;
	ret.s = -1L;

	char * buf = malloc(sizeof(char)*(size+1));
	if(buf == NULL) {
		fprintf(stderr, "malloc failed: %s\n", strerror(errno));
		return ret;
	}

	memset(buf, 'a', size);		// TODO: Randomize data
	buf[size] = '\0';

	// Send size
	char msg[9] = {'\0'};
	sprintf(msg, "%ld", size);
	if(send(sock, msg, 8, 0) < 0) {
		fprintf(stderr, "send failed: %s\n", strerror(errno));
		free(buf);
		return ret;
	}
	if(recv(sock, msg, 8, MSG_WAITALL) < 8) {
		fprintf(stderr, "recv failed: %s\n", strerror(errno));
		free(buf);
		return ret;
	}
	if(!strcmp("OK", msg)) {
		fprintf(stderr, "Illegal response\n");
		free(buf);
		return ret;
	}

	// Send packet
	struct timeval t1, t2, t3, t_delta;
	gettimeofday(&t1, NULL);
	ssize_t slen = send(sock, buf, size, 0);
	gettimeofday(&t2, NULL);
	if(slen < 0) {
		fprintf(stderr, "send_bw failed: %s\n", strerror(errno));
		return ret;
	}
	timersub(&t2, &t1, &t_delta);
	ret.f = (t_delta.tv_usec + t_delta.tv_sec * 1000L*1000L);
	
	// Now wait for the data
	slen = recv(sock, buf, size, MSG_WAITALL);
	free(buf);
	gettimeofday(&t3, NULL);
	if(slen < 0) {
		fprintf(stderr, "recv_bw failed: %s\n", strerror(errno));
		return ret;
	} else if((size_t)slen < size) {
		fprintf(stderr, "incomplete received: %ld/%ld\n", slen, size);
		return ret;
	}
	timersub(&t3, &t2, &t_delta);
	ret.s = (t_delta.tv_usec + t_delta.tv_sec * 1000L*1000L);

	return ret;
}

void warmup(const int sock, int seconds) {
	struct timeval t1, t2, t_delta;
	long size = 10240;


	gettimeofday(&t1, NULL);
	while(true) {
		gettimeofday(&t2, NULL);
		timersub(&t2, &t1, &t_delta);
		if(((long)t_delta.tv_sec+t_delta.tv_usec/(1000L*1000L)) > (long)seconds) break;
		pair_l ret = bw_test(sock, size);
		if(ret.f < 0 || ret.s < 0) {
			fprintf(stderr,"warumup failed\n");
			return;
		}
	}
}

int run_client(const char* remote, const int port) {
	int sock = 0;
    struct sockaddr_in addr; 
    memset(&addr, 0, sizeof(addr)); 
      
    // Filling server information 
    addr.sin_family = AF_INET; 
    addr.sin_port = htons(port); 
    addr.sin_addr.s_addr = inet_addr(remote); 


    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
    	fprintf(stderr, "Error creating socket: %s\n", strerror(errno));
    	return -1;
    }
	socklen_t addrlen = sizeof(addr);
	int rc = connect(sock, (const struct sockaddr *)&addr, addrlen);
	if(rc < 0) {
		fprintf(stderr, "Connect failed: %s\n", strerror(errno));
		close(sock);
		exit(EXIT_FAILURE);
	}
	// Disable Nagle's algorithm for ping 
	int one = 1;
	if(setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(int)) < 0)
		fprintf(stderr, "Warning: Failed to set TCP_NODELAY for new socket: %s\n", strerror(errno));


	// First run a warmup
	if(warmup_s > 0) {
		printf("Warmup %d seconds ... \n", warmup_s);
		warmup(sock, warmup_s);
	}

	long bytes[] = {128L,256L,512L,1024L,2048L,4096L,10240L,40960L,81920L,122880L,163840L,204800L,327680L,409600L,819200L,1228800L,1638400L, 3276800L, 4915200L, 6553600L, 65536000L};
	const int nTests =(sizeof(bytes)/sizeof(bytes[0]));
	printf("Running %d tests with %d iterations each\n\n", nTests, SERIES);
	printf("%10s\t%5s\t%5s\t%5s [µs]\n","Size", "Avg", "Min", "Max");
	double max_speed = 0;
	for(size_t i=0;i<nTests;i++) {
		long size = bytes[i];

		long tests[SERIES];
		for(int i=0;i<SERIES;i++) {
			pair_l l = bw_test(sock, size);
			if(l.f < 0 || l.s < 0) {
				fprintf(stderr, "error: %s\n", strerror(errno));
				exit(EXIT_FAILURE);
			}
			tests[i] = (l.f + l.s) / 2L;
		}
		stats_l st = stats(tests, SERIES);
		
		printf("%10ld\t%5ld\t%5ld\t%5ld\n", size, st.avg, st.min, st.max);
		double speed = size / (st.min) * 1e6;		// Bytes/s
		if(speed > max_speed) max_speed = speed;
	}
	printf("Maximum throughput: ");
	if(max_speed > 2e9) {
		double gb = max_speed / (1024.0*1024.0*1024.0);
		printf("%5.2f GiB/s (%5.2f GBit/sec)\n", gb, gb*8.0);
	} else if(max_speed > 2e6) {
		double mb = max_speed / (1024.0*1024.0);
		printf("%5.2f MiB/s (%5.2f MBit/sec)\n", mb, mb*8.0);
	} else if(max_speed > 2e3) {
		double kb = max_speed / (1024.0);
		printf("%5.2f KiB/s (%5.2f kBit/sec)\n", kb, kb*8.0);
	} else {
		printf("%5.2f B/s (%5.2f Bit/sec)\n", max_speed, max_speed*8.0);
	}

	// Close socket
	char msg[8];
	sprintf(msg, "CLOSE");
	send(sock, msg, 8, 0);
	close(sock);
	return 0;
}
