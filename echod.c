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
#include <netinet/tcp.h>

#define BUF_SIZE 4096


static int port = 7; // See https://tools.ietf.org/html/rfc862
static int sock_udp = 0;
static int sock_tcp = 0;
static pthread_t tid_udp = 0;
static pthread_t tid_tcp = 0;
static volatile size_t bytes_udp = 0;
static volatile size_t bytes_tcp = 0;
static volatile bool running = true;


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
    bool tcp = true;
    bool daemon = false;
    uid_t uid = 0;
    gid_t gid = 0;
    char *w_dir = NULL;
    
    for(int i=1;i<argc;i++) {
    	const char* arg = argv[i];
    	if(strlen(arg) == 0) continue;
    	if(arg[0] == '-') {
    		if(!strcmp("-h", arg) || !strcmp("--help", arg)) {
    			printf("Stupid simple echo server\n");
    			printf("  2019, Felix Niederwanger\n\n");
    			printf("Usage: %s [OPTIONS] [PORT]\n", argv[0]);
    			printf("OPTIONS:\n");
    			printf("  -h, --help            Print this help message\n");
    			printf("  -u, --udp             Enable udp server\n");
    			printf("  -t, --tcp             Enable tcp server\n");
    			printf("      --noudp           Disable udp server\n");
    			printf("      --notcp           Disable tcp server\n");
    			printf("  -d, --daemon          Run as daemon\n");
    			printf("      --user UID        Run as user UID\n");
    			printf("      --group GID       Run as group GID\n");
    			printf("      --chdir DIR       chdir to DIR\n");
				exit(EXIT_SUCCESS);
    		} else if(!strcmp("-d", arg) || !strcmp("--daemon", arg)) {
    			daemon = true;
    		} else if(!strcmp("-u", arg) || !strcmp("--udp", arg)) {
    			udp = true;
    		} else if(!strcmp("-t", arg) || !strcmp("--tcp", arg)) {
    			tcp = true;
    		} else if(!strcmp("--noudp", arg)) {
    			udp = false;
    		} else if(!strcmp("--notcp", arg)) {
    			tcp = false;
    		} else if(!strcmp("--user", arg)) {
    			uid = (uid_t)atoi(argv[++i]);
    		} else if(!strcmp("--group", arg)) {
    			gid = (gid_t)atoi(argv[++i]);
    		} else if(!strcmp("--chdir", arg)) {
    			w_dir = argv[++i];
    		}
    	} else
    		port = atoi(argv[1]);
    }

    if (daemon) { // Fork daemon
    		pid_t pid = fork();
		if(pid < 0) {
			fprintf(stderr, "Forking failed: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		} else if(pid > 0)
			exit(EXIT_SUCCESS); // Success. The parent leaves here
		pid = fork();
		if(pid < 0) {
			fprintf(stderr, "Forking failed: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		} else if(pid > 0) 
			exit(EXIT_SUCCESS);	// Success. The parent again leaves here
    }

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
    
    // Drop privileges, if root
    if (getuid() == 0) {
    	if(gid > 0) {
    	
	    	if (setgid(gid) != 0) {
	    		fprintf(stderr, "Error setting gid to %d: %s\n", gid, strerror(errno));
	    		exit(EXIT_FAILURE);
	    	}
	    }
	    if(uid > 0) {
	    	if (setuid(uid) != 0) {
	    		fprintf(stderr, "Error setting uid to %d: %s\n", uid, strerror(errno));
	    		exit(EXIT_FAILURE);
	    	}
	    }
	    if(w_dir != NULL) {
	    	if(chdir(w_dir) != 0) {
	    		fprintf(stderr, "Error changing to '%s': %s\n", w_dir, strerror(errno));
	    		exit(EXIT_FAILURE);
	    	
	    	}
	    }
    }
    
    // Wait for threads to terminate
    if(tid_udp > 0) {
    	pthread_join(tid_udp, NULL);
    }
    if(tid_tcp > 0) {
    	pthread_join(tid_tcp, NULL);
    }

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

void * tcp_client(void * args) {
	// Make parameters thread-local and free memory
	s_thread_params_t *params = (s_thread_params_t*)args;
	const int fd = params->sock;
	free(params);

	// Disable Nagle's algorithm
	int one = 1;
	if(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(int)) < 0)
		fprintf(stderr, "Warning: Failed to set TCP_NODELAY for new socket: %s\n", strerror(errno));

	char buf[BUF_SIZE];
	while(running) {
		ssize_t len = recv(fd, buf, BUF_SIZE, 0);
		if(len <= 0) goto finish;
		ssize_t slen = send(fd, buf, len, MSG_DONTWAIT);
		if(slen < 0) goto finish;
		bytes_tcp += len;		// XXX atomic add
	}

finish:
	close(fd);
	return NULL;
}

void * server_thread(void * args) {
	// Make parameters thread-local and free memory
	s_thread_params_t *params = (s_thread_params_t*)args;
	const int fd = params->sock;
	const bool udp = params->udp;
	free(params);

	if (udp) {
		char buf[BUF_SIZE];
		
		while(true) {
			struct sockaddr_in src_addr;
			socklen_t addrlen = sizeof(src_addr);
			ssize_t len = recvfrom(fd, buf, BUF_SIZE, MSG_WAITALL, (struct sockaddr*)&src_addr, &addrlen);
			if(!running) goto finish;
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
	} else {
		// I am a listener server
		if(listen(fd, 10) < 0) {
			fprintf(stderr, "listening failed: %s\n", strerror(errno));
			goto finish;
		}

		while(true) {
			const int sock = accept(fd, NULL, NULL);
			if(!running) goto finish;
			if(sock <= 0) {
				fprintf(stderr, "accept error: %s\n", strerror(errno));
				goto finish;
			}

			// Background thread
			pthread_t tid;
			s_thread_params_t *params = (s_thread_params_t*)malloc(sizeof(s_thread_params_t));
			if(params == NULL) {
				fprintf(stderr, "out of memory");
				goto finish;
			}
			params->sock = sock;
			params->udp = false;
			int rc = pthread_create(&tid, NULL, tcp_client, params);
			if(rc < 0) {
				free(params);
				fprintf(stderr, "error creating tcp client thread: %s\n", strerror(errno));
				goto finish;
			}
			pthread_detach(tid);
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
			running = false;

			fprintf(stderr, "SIGINT received\n");
			if(sock_udp > 0) shutdown(sock_udp, SHUT_RDWR);
			if(sock_tcp > 0) shutdown(sock_tcp, SHUT_RDWR);
			return;
		case SIGTERM:
			emergency = true;
			running = false;
			if(sock_udp > 0) shutdown(sock_udp, SHUT_RDWR);
			if(sock_tcp > 0) shutdown(sock_tcp, SHUT_RDWR);
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
