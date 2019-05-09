/* =============================================================================
 * 
 * Title:         Simple network throughput test program
 * Author:        Felix Niederwanger
 * License:       Copyright (c), 2018 Felix Niederwanger
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

// Number of runs per series
#define SERIES 10
#define DISABLE_NAGLE 0

static char *remote = "";
static int port = 7;
static int iterations = 10;


static void throughput_test(const struct sockaddr_in *remote);


int main(int argc, char** argv) {
	if(argc < 2) {
		printf("Usage: %s [OPTIONS] REMOTE [PORT]\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	for(int i=1;i<argc;i++) {
		const char* arg = argv[i];
		if(strlen(arg) == 0) continue;
		if(arg[0] == '-') {
			if(!strcmp("-h", arg) || !strcmp("--help", arg)) {
				printf("Stupid simple network throughput test program\n");
				printf("  2019 Felix Niederwanger\n");
				printf("\n");
				printf("Usage: %s [OPTIONS] REMOTE [PORT]\n", argv[0]);
				printf("OPTIONS\n");
				printf("  -h, --help                 Print this help message\n");
				printf("  -i, --iterations N         Set number of iterations (default: 10)\n");
				printf("REMOTE:PORT must be an endpoint with 'echo' running (tcp only!)\n");
				printf("\n");
				printf("https://github.com/grisu48/pingpong\n");
				exit(EXIT_SUCCESS);
			} else if(!strcmp("-i", arg) || !strcmp("--iterations", arg)) {
				// XXX: Out of bound check
				iterations = atoi(argv[++i]);
			} else {
				fprintf(stderr, "Illegal argument: %s\n", arg);
				printf("Type %s --help if you need help\n", argv[0]);
				exit(EXIT_FAILURE);
			}
		} else {
			if(strlen(remote) == 0)
				remote = (char*)arg;
			else
				port = atoi(arg);
		}
	}
  
    struct sockaddr_in addr; 
    memset(&addr, 0, sizeof(addr)); 
    addr.sin_family = AF_INET; 
    addr.sin_port = htons(port); 
    addr.sin_addr.s_addr = inet_addr(remote); 

    throughput_test((const struct sockaddr_in *)&addr);

    exit(EXIT_SUCCESS);
}

static long a_max(const long* arr, const size_t n) {
	long ret = arr[0];
	for(size_t i=0;i<n;i++) {
		if(arr[i] < 0) continue;
		if(arr[i] > ret) ret = arr[i];
	}
	return ret;
}

static long a_min(const long* arr, const size_t n) {
	long ret = a_max(arr,n);		// Make sure we don't take -1 by accident!
	for(size_t i=0;i<n;i++) {
		if(arr[i] < 0) continue;
		if(arr[i] < ret) ret = arr[i];
	}
	return ret;
}

static double a_avg(const long* arr, const size_t n) {
	double sum = 0;
	long elems = 0;
	for(size_t i=0;i<n;i++) {
		if(arr[i] < 0) continue;
		sum += arr[i];
		elems++;
	}
	return sum/(double)elems;
}



long tcp_connect(int sock, const struct sockaddr *remote) {
	socklen_t addrlen = sizeof(struct sockaddr);

	struct timeval t1, t2, t_delta;
	gettimeofday(&t1, NULL);
	int rc = connect(sock, (const struct sockaddr *)remote, addrlen);
	gettimeofday(&t2, NULL);
	if(rc < 0) return -1;
	timersub(&t2, &t1, &t_delta);
	return (t_delta.tv_usec + t_delta.tv_sec * 1000L*1000L);
}

long tcp_sendrecv(const int sock, const size_t len, const int n, const size_t buf_len) {
	char *buf = (char*)malloc(sizeof(char)*buf_len);
	if(buf == NULL) return -1;
	long ret = -1;

	// XXX Randomize data?
	memset(buf, 'a', buf_len);

	struct timeval t1, t2, t_delta;
	gettimeofday(&t1, NULL);
	for(int i=0;i<n;i++) {
		size_t remaining = len;

		while(remaining > 0) {
			size_t b = (buf_len<remaining?buf_len:remaining);
			ssize_t slen = send(sock, buf, b, MSG_DONTWAIT);
			if(slen < 0) goto fail;
			slen = recv(sock, buf, b, MSG_WAITALL);
			if(slen < 0) goto fail;
			remaining -= b;
		}

	}
	gettimeofday(&t2, NULL);
	timersub(&t2, &t1, &t_delta);
	ret = t_delta.tv_usec + t_delta.tv_sec * 1000L*1000L;

	free(buf); 
	return ret/n;
fail:
	fprintf(stderr, "%s\n", strerror(errno));
	free(buf);
	return -1;
}

static void throughput_test(const struct sockaddr_in *remote) {
	long bytes[] = {128L,256L,512L,1024L,2048L,4096L,10240L,40960L,81920L,122880L,163840L,204800L,327680L,409600L,819200L,1228800L,1638400L, 3276800L, 4915200L, 6553600L};
	const size_t buf_len = 10240L;		// Make sure it's large enough (ib has sometimes 4k!)
	int sock;

	printf("## ==== TCP throughput ====================================================== ##\n");

	sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
    	fprintf(stderr, "Error creating socket: %s\n", strerror(errno));
        exit(EXIT_FAILURE); 
    }

	long rtt = tcp_connect(sock, (const struct sockaddr*)remote);
	if(rtt < 0) {
    	fprintf(stderr, "Connect failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE); 
	} else {
		printf("; Connect\t%ld µsec\n", rtt);
	}

#if DISABLE_NAGLE == 1
	// Disable Nagle's algorithm
	int one = 1;
	if(setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(int)) < 0) {
		fprintf(stderr, "Failed to set TCP_NODELAY for new socket: %s\n", strerror(errno));
		goto finish;
	} else {
		printf("# TCP_NODELAY = 1\n");
	}
#endif

	printf("# Size\t%8s\t%8s\t%8s\n", "Average [MB/s]", "Worst [MB/s]", "Best [MB/s]");

	for(size_t i=0;i<(sizeof(bytes)/sizeof(bytes[0]));i++) {
		long rtt[SERIES];
		const long size = bytes[i];
		for(int j=0;j<SERIES;j++) {
			rtt[j] = tcp_sendrecv(sock, size, iterations, buf_len);
		}

		long t_avg = a_avg(rtt, SERIES);
		long t_min = a_min(rtt, SERIES);
		long t_max = a_max(rtt, SERIES);

		double s_avg = size/(t_avg*1e-6)/(1024.0*1024.0);
		double s_min = size/(t_max*1e-6)/(1024.0*1024.0);
		double s_max = size/(t_min*1e-6)/(1024.0*1024.0);

		printf("%ld\t%8.2f\t%8.2f\t%8.2f\n", bytes[i], s_avg, s_min, s_max);


	}

	printf("## ========================================================================== ##\n");



finish:
	close(sock);
}
