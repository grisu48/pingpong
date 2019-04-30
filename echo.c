/* =============================================================================
 * 
 * Title:         Simple tcp/udp server
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
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define BUF_SIZE 4096


static int port = 7; // See https://tools.ietf.org/html/rfc862
static int sock_udp = 0;
static int sock_tcp = 0;
static pthread_t tid_udp = 0;
static pthread_t tid_tcp = 0;
static volatile size_t bytes_udp = 0;
static volatile size_t bytes_tcp = 0;


/** Create udp server on the given port
  * @param port Port to listen on
  * @param pid thread id
  * @param sock socket that is created
  * @returns 0 on success, negative value on error and setting errno accordingly */
int udp_server(const int port, pthread_t *pid, int *sock);

/** Create tcp server on the given port
  * @param port Port to listen on
  * @param pid thread id
  * @param sock socket that is created
  * @returns 0 on success, negative value on error and setting errno accordingly */
int tcp_server(const int port, pthread_t *pid, int *sock);

void sig_handler(int signo);

void cleanup();

int main(int argc, char** argv) {
    bool udp = true;
    bool tcp = false;
    
    if(argc > 1)
    	port = atoi(argv[1]);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGUSR1, sig_handler);
    atexit(cleanup);

    if(udp) {
    	int rc = udp_server(port, &tid_udp, &sock_udp);
    	if(rc != 0) {
    		fprintf(stderr, "Error creating udp server: %s\n", strerror(errno));
    		exit(EXIT_FAILURE);
    	}
    }
    if(tcp) {
    	int rc = tcp_server(port, &tid_tcp, &sock_tcp);
    	if(rc != 0) {
    		fprintf(stderr, "Error creating tcp server: %s\n", strerror(errno));
    		exit(EXIT_FAILURE);
    	}
    }
    
    // Wait for threads to terminate
    if(tid_udp > 0)
    	pthread_join(tid_udp, NULL);
    if(tid_tcp > 0)
    	pthread_join(tid_tcp, NULL);

	if (sock_udp > 0)
		printf("udp server handled %ld bytes\n", bytes_udp);
	if (sock_tcp > 0)
		printf("tcp server handled %ld bytes\n", bytes_tcp);

    return EXIT_SUCCESS;
}

typedef struct {
	int sock;
	bool udp;
} s_thread_params_t;

void * server_thread(void * args) {
	// Make parameters thread-local and free memory
	s_thread_params_t *params = (s_thread_params_t*)args;
	const int fd = params->sock;
	const bool udp = params->udp;
	free(params);

	if (udp) {
		char buf[BUF_SIZE];
		
		while(fd > 0) {
			struct sockaddr_in src_addr;
			socklen_t addrlen = sizeof(src_addr);
			ssize_t len = recvfrom(fd, buf, BUF_SIZE, MSG_WAITALL, (struct sockaddr*)&src_addr, &addrlen);
			if(len <= 0) {
				fprintf(stderr, "udp receive error: %s\n", strerror(errno));
				goto finish;
			}
			int flags = MSG_DONTWAIT;
			len = sendto(fd, buf, len, flags, (const struct sockaddr *)&src_addr, addrlen);
			if(len <= 0) {
				fprintf(stderr, "udp send error: %s\n", strerror(errno));
				goto finish;
			}
			bytes_udp += len;		// TODO: atomic increment
		} 
}

finish:
	close(fd);
	return NULL;
}

static int create_server_thread(int sock, bool udp, pthread_t *tid) {
	s_thread_params_t *params = (s_thread_params_t*)malloc(sizeof(s_thread_params_t));
	if(params == NULL) {
		errno = ENOMEM;
		return -ENOMEM;
	}
	params->sock = sock;
	params->udp = udp;
	int rc = pthread_create(tid, NULL, server_thread, params);
	if(rc < 0) {
		free(params);
		return rc;
	}
	return rc;
}

int udp_server(const int port, pthread_t *pid, int *sock) {
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	int rc = 0;
	if(fd < 0) return fd;

	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));
	addr.sin_family    = AF_INET; // IPv4 
    addr.sin_addr.s_addr = INADDR_ANY; 
    addr.sin_port = htons(port); 

    rc = bind(fd, (const struct sockaddr*)&addr, sizeof(addr));
    if(rc < 0) goto fail;
    rc = create_server_thread(fd, true, pid);
    if(rc < 0) goto fail;

    *sock = fd;
    return rc;
fail:
	close(fd);
	return rc;
}

int tcp_server(const int port, pthread_t *pid, int *sock) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	int rc = 0;
	if(fd < 0) return fd;

	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));
	addr.sin_family    = AF_INET; // IPv4 
    addr.sin_addr.s_addr = INADDR_ANY; 
    addr.sin_port = htons(port); 

    rc = bind(fd, (const struct sockaddr*)&addr, sizeof(addr));
    if(rc < 0) goto fail;
    rc = create_server_thread(fd, false, pid);
    if(rc < 0) goto fail;

    *sock = fd;
    return rc;
fail:
	close(fd);
	return rc;
}

void sig_handler(int signo) {
	static bool emergency = false;	// Emergency exit on second SIGINT
	switch(signo) {
		case SIGINT:
			if(emergency) exit(EXIT_FAILURE);
			emergency = true;	

			fprintf(stderr, "SIGINT received\n");
			if(sock_udp > 0) shutdown(sock_udp, SHUT_RDWR);
			if(sock_tcp > 0) close(sock_tcp);
			return;
		case SIGTERM:
			if(sock_udp > 0) close(sock_udp);
			if(sock_tcp > 0) close(sock_tcp);
			exit(EXIT_FAILURE);
			return;
		case SIGUSR1:
			if (sock_udp > 0)
				printf("udp:%d - %ld bytes\n", port, bytes_udp);
			if (sock_tcp > 0)
				printf("tcp:%d - %ld bytes\n", port, bytes_tcp);
			return;
	}
}

void cleanup() {
}
