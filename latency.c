/* =============================================================================
 * 
 * Title:         Simple network latency test program
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

static char *remote = "";
static int port = 7;
static int iterations = 10;


static void udp_tests(const struct sockaddr_in *remote);
static void tcp_tests(const struct sockaddr_in *remote);


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
				printf("Stupid simple latency test program\n");
				printf("  2019 Felix Niederwanger\n");
				printf("\n");
				printf("Usage: %s [OPTIONS] REMOTE [PORT]\n", argv[0]);
				printf("OPTIONS\n");
				printf("  -h, --help                 Print this help message\n");
				printf("REMOTE:PORT must be an endpoint with 'echo' running (tcp+udp)\n");
				printf("\n");
				printf("https://github.com/grisu48/pingpong\n");
				exit(EXIT_SUCCESS);
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

    udp_tests((const struct sockaddr_in *)&addr);
    tcp_tests((const struct sockaddr_in *)&addr);

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




long udp_ping(int sock, const struct sockaddr *addr, size_t len, int n) {
	char *buf = (char*)malloc(sizeof(char)*len);
	if(buf == NULL) return -1;
	long ret = -1;

	memset(buf, 'a', len);

	struct timeval t1, t2, t_delta;
	gettimeofday(&t1, NULL);
	for(int i=0;i<n;i++) {
		ssize_t slen = sendto(sock, buf, len, MSG_DONTWAIT, addr, sizeof(struct sockaddr_in));
		if(slen < 0) goto fail;
		if((size_t)slen != len) {
			fprintf(stderr, "Error sending %ld bytes - only sent %ld\n", len, slen);
			return -1;
		}
		struct sockaddr src_addr;
		socklen_t addrlen = sizeof(src_addr);
		slen = recvfrom(sock, buf, len, MSG_WAITALL, &src_addr, &addrlen);
		// NOTE: We can only receive up to MTU size packets as of now
		if(slen < 0) goto fail;
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

static void udp_tests(const struct sockaddr_in *remote) {
	long bytes[] = {1,2,4,8,16,32,56,128,256,512};
	int sock;

	printf("## ==== UDP latency ========================================================= ##\n");

	sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0 ) {
    	fprintf(stderr, "Error creating socket: %s\n", strerror(errno));
        exit(EXIT_FAILURE); 
    } 

	// We don't fragment UDP packets as of now
	int one = 1;
	if(setsockopt(sock, IPPROTO_IP, IP_PMTUDISC_DO, &one, sizeof(one)) < 0) {
		fprintf(stderr, "Error setting the don't fragment flag: %s\n", strerror(errno));
		goto finish;
	}



	printf("# Size	Average	Best	Worst\n");

	for(size_t i=0;i<(sizeof(bytes)/sizeof(bytes[0]));i++) {
		long rtt[SERIES];
		for(int j=0;j<SERIES;j++) {
			rtt[j] = udp_ping(sock, (const struct sockaddr*)remote, bytes[i], iterations);
		}

		printf("%ld\t%.0f\t%ld\t%ld\n", bytes[i], a_avg(rtt, SERIES), a_min(rtt, SERIES), a_max(rtt, SERIES));

	}

finish:
	close(sock);
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

long tcp_ping(int sock, size_t len, int n) {
	char *buf = (char*)malloc(sizeof(char)*len);
	if(buf == NULL) return -1;
	long ret = -1;

	memset(buf, 'a', len);

	struct timeval t1, t2, t_delta;
	gettimeofday(&t1, NULL);
	for(int i=0;i<n;i++) {
		ssize_t slen = send(sock, buf, len, MSG_DONTWAIT);
		if(slen < 0) goto fail;
		slen = recv(sock, buf, len, MSG_WAITALL);
		if(slen < 0) goto fail;
		if((size_t)slen < len) {
			fprintf(stderr, "received less bytes than sent (%ld < %ld)\n", slen, len);
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

static void tcp_tests(const struct sockaddr_in *remote) {
	long bytes[] = {1,2,4,8,16,32,56,128,256,512,1024,2048,4096,10240,40960};
	int sock;

	printf("## ==== TCP latency ========================================================= ##\n");

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

	// Disable Nagle's algorithm for ping 
	int one = 1;
	if(setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(int)) < 0) {
		fprintf(stderr, "Failed to set TCP_NODELAY for new socket: %s\n", strerror(errno));
		goto finish;
	} else {
		printf("# TCP_NODELAY = 1\n");
	}

	printf("# Size	Average	Best	Worst\n");

	for(size_t i=0;i<(sizeof(bytes)/sizeof(bytes[0]));i++) {
		long rtt[SERIES];
		for(int j=0;j<SERIES;j++) {
			rtt[j] = tcp_ping(sock, bytes[i], iterations);
		}

		printf("%ld\t%.0f\t%ld\t%ld\n", bytes[i], a_avg(rtt, SERIES), a_min(rtt, SERIES), a_max(rtt, SERIES));


	}

	printf("## ========================================================================== ##\n");



finish:
	close(sock);
}