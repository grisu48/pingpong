/* =============================================================================
 * 
 * Title:         Simple tcp ping utility
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

/**
  * Pings n times on the given socket with the given len
  */
long ping(int sock, const struct sockaddr *addr, size_t len, int n) {
	char *buf = (char*)malloc(sizeof(char)*len);
	if(buf == NULL) return -1;
	long ret = -1;

	memset(buf, 'a', len);

	struct timeval t1, t2, t_delta;
	gettimeofday(&t1, NULL);
	for(int i=0;i<n;i++) {
		ssize_t slen = send(sock, buf, len, 0);
		//printf("sendto = %ld\n", slen);
		if(slen < 0) goto fail;
		// TODO: Lazy read!
		slen = recv(sock, buf, len, MSG_WAITALL);
		//printf("recvfrom = %ld\n", slen);
		if(slen < 0) goto fail;
	}
	gettimeofday(&t2, NULL);
	timersub(&t2, &t1, &t_delta);
	ret = t_delta.tv_usec + t_delta.tv_sec * 1000L*1000L;

	free(buf); 
	return ret;
fail:
	fprintf(stderr, "%s\n", strerror(errno));
	free(buf);
	return -1;
}

int main(int argc, char** argv) {
	char *remote = "";
	int port = 7;
	int sock;

	if(argc < 2) {
		printf("Usage: %s REMOTE [PORT]\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	remote = argv[1];
	if(argc > 2)
		port = atoi(argv[2]);

  
    // Creating socket file descriptor 
    if ( (sock = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
    	fprintf(stderr, "Error creating socket: %s\n", strerror(errno));
        exit(EXIT_FAILURE); 
    } 
  

    struct sockaddr_in addr; 
    memset(&addr, 0, sizeof(addr)); 
      
    // Filling server information 
    addr.sin_family = AF_INET; 
    addr.sin_port = htons(port); 
    addr.sin_addr.s_addr = inet_addr(remote); 

    long iterations = 100;

	// Connect socket
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

    printf("   Bytes    RTT [usec]\n");
    for(int i=0;i<15;i++) {
    	size_t bytes = pow(2,i);

    	for(int j=0;j<3;j++) {
	    	long rtt = ping(sock, (struct sockaddr*)&addr, bytes, iterations);
	    	printf("%8ld ", bytes);
	    	if(rtt < 0) {
	    		printf("err\n");
	    	} else {
	    		printf("%8ld\n", rtt/iterations);
	    	}
    	}
    }

    close(sock);
    return EXIT_SUCCESS;
    
}
