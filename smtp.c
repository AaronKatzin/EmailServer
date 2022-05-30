
// includes from select server
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
// END includes from select server

#include <syslog.h>
#include <pthread.h>
#include <ctype.h>

#define BACKLOG_MAX	(10)
#define BUF_SIZE	4096
#define STR_EQUAL(a,b)	(strcasecmp(a, b) == 0) // case insensitive compare as required by spec

#define DOMAIN	"127.0. 0.1"
#define PORT		"2525"

int debug_i = 0;

// Linked list of ints container
struct int_ll {
	int d;
	struct int_ll *next;
};

// Overall server state
struct {
	struct int_ll *sockfds;
	int sockfd_max;
	char *domain;
	pthread_t thread; // Latest spawned thread
} state;

// Function prototypes
void init_socket(void);
void *handle_smtp (void *thread_arg);
void *get_in_addr(struct sockaddr *sa);

int main (int argc, char *argv[]) {


	// Init syslog with program prefix
	char *syslog_buf = (char*) malloc(1024);
	sprintf(syslog_buf, "%s", argv[0]);
	openlog(syslog_buf, LOG_PERROR | LOG_PID, LOG_USER);


	state.domain = DOMAIN;
	char ipbuf[INET6_ADDRSTRLEN];

	// Open sockets to listen on for client connections
	init_socket();

	// Loop forever listening for connections and spawning
	// threads to handle each exchange via handle_smtp()
	while (1) {
		fd_set sockets;
		FD_ZERO(&sockets);
		struct int_ll *p;

		for (p = state.sockfds; p != NULL; p = p->next) {
			FD_SET(p->d, &sockets);
		}

		// Wait forever for a connection on any of the bound sockets
		select (state.sockfd_max+1, &sockets, NULL, NULL, NULL);

		// Iterate through the sockets looking for one with a new connection
		for (p = state.sockfds; p != NULL; p = p->next) {
			if (FD_ISSET(p->d, &sockets)) {
				struct sockaddr_storage client_addr;
				socklen_t sin_size = sizeof(client_addr);
				int new_sock = accept (p->d, \
						(struct sockaddr*) &client_addr, &sin_size);
				if (new_sock == -1) {
					syslog(LOG_ERR, "Accepting client connection failed");
					continue;
				}

				// Convert client IP to human-readable
				void *client_ip = get_in_addr(\
						(struct sockaddr *)&client_addr);
				inet_ntop(client_addr.ss_family, \
						client_ip, ipbuf, sizeof(ipbuf));
				syslog(LOG_DEBUG, "Connection from %s", ipbuf);

				// Pack the socket file descriptor into dynamic mem
				// to be passed to thread; it will free this when done.
				int * thread_arg = (int*) malloc(sizeof(int));
				*thread_arg = new_sock;

				// Spawn new thread to handle SMTP exchange
				pthread_create(&(state.thread), NULL, \
						handle_smtp, thread_arg);

			}
		}
	} // end forever loop

	return 0;
}

// Try to bind to as many local sockets as available.
// Typically this would just be one IPv4 and one IPv6 socket
void init_socket(void) {
	int rc, i, j, yes = 1;
	int sockfd;
	struct addrinfo hints, *hostinfo, *p;

	// Set up the hints indicating all of localhost's sockets
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	state.sockfds = NULL;
	state.sockfd_max = 0;

	rc = getaddrinfo(NULL, PORT, &hints, &hostinfo);
	if (rc != 0) {
		syslog(LOG_ERR, "Failed to get host addr info");
		exit(EXIT_FAILURE);
	}

	for (p=hostinfo; p != NULL; p = p->ai_next) {
		void *addr;
		char ipstr[INET6_ADDRSTRLEN];
		if (p->ai_family == AF_INET) {
			addr = &((struct sockaddr_in*)p->ai_addr)->sin_addr;
		} else {
			addr = &((struct sockaddr_in6*)p->ai_addr)->sin6_addr;
		}
		inet_ntop(p->ai_family, addr, ipstr, sizeof(ipstr));

		sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sockfd == -1) {
			syslog(LOG_NOTICE, "Failed to create IPv%d socket", \
					(p->ai_family == AF_INET) ? 4 : 6 );
			continue;
		}

		setsockopt(sockfd, SOL_SOCKET, \
				SO_REUSEADDR, &yes, sizeof(int));

		rc = bind(sockfd, p->ai_addr, p->ai_addrlen);
		if (rc == -1) {
			close (sockfd);
			syslog(LOG_NOTICE, "Failed to bind to IPv%d socket", \
					(p->ai_family == AF_INET) ? 4 : 6 );
			continue;
		}

		rc = listen(sockfd, BACKLOG_MAX);
		if (rc == -1) {
			syslog(LOG_NOTICE, "Failed to listen to IPv%d socket", \
					(p->ai_family == AF_INET) ? 4 : 6 );
			exit(EXIT_FAILURE);
		}

		// Update highest fd value for select()
		(sockfd > state.sockfd_max) ? (state.sockfd_max = sockfd) : 1;

		// Add new socket to linked list of sockets to listen to
		struct int_ll *new_sockfd = malloc(sizeof(struct int_ll));
		new_sockfd->d = sockfd;
		new_sockfd->next = state.sockfds;
		state.sockfds = new_sockfd;
	}

	if (state.sockfds == NULL) {
		syslog(LOG_ERR, "Completely failed to bind to any sockets");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(hostinfo);
	return;
}

void *handle_smtp (void *thread_arg) {
	syslog(LOG_DEBUG, "Starting thread for socket #%d", *(int*)thread_arg);

	int rc, i, j;
	char buffer[BUF_SIZE], bufferout[BUF_SIZE];
	int buffer_offset = 0;
	buffer[BUF_SIZE-1] = '\0';

	// Unpack dynamic mem argument from main()
	int sockfd = *(int*)thread_arg;
	free(thread_arg);

	// Flag for being inside of DATA verb
	int indata = 0;

	sprintf(bufferout, "220 %s SMTP CCSMTP\r\n", state.domain);
	printf("%s", bufferout);
	send(sockfd, bufferout, strlen(bufferout), 0);

	while (1) {
		fd_set sockset;
		struct timeval tv;

		FD_ZERO(&sockset);
		FD_SET(sockfd, &sockset);
		tv.tv_sec = 120; // Some SMTP servers pause for ~15s per message
		tv.tv_usec = 0;

		// Wait tv timeout for the server to send anything.
		select(sockfd+1, &sockset, NULL, NULL, &tv);

		if (!FD_ISSET(sockfd, &sockset)) {
			syslog(LOG_DEBUG, "%d: Socket timed out", sockfd);
			break;
		}

		int buffer_left = BUF_SIZE - buffer_offset - 1;
		if (buffer_left == 0) {
			syslog(LOG_DEBUG, "%d: Command line too long", sockfd);
			sprintf(bufferout, "500 Too long\r\n");
			printf("S%d: %s", sockfd, bufferout);
			send(sockfd, bufferout, strlen(bufferout), 0);
			buffer_offset = 0;
			continue;
		}

		rc = recv(sockfd, buffer + buffer_offset, buffer_left, 0);
		if (rc == 0) {
			syslog(LOG_DEBUG, "%d: Remote host closed socket", sockfd);
			break;
		}
		if (rc == -1) {
			syslog(LOG_DEBUG, "%d: Error on socket", sockfd);
			break;
		}

		buffer_offset += rc;

		char *eol;

		// Only process one line of the received buffer at a time
		// If multiple lines were received in a single recv(), goto
		// back to here for each line
		//
processline:
		eol = strstr(buffer, "\r\n");
		if (eol == NULL) {
			syslog(LOG_DEBUG, "%d: Haven't found EOL yet", sockfd);
			continue;
		}

		// Null terminate each line to be processed individually
		eol[0] = '\0';

		if (!indata) { // Handle system verbs
			printf("C%d: %s\n", sockfd, buffer);

			// Replace all lower case letters so verbs are all caps
			for (i=0; i<4; i++) {
				if (islower(buffer[i])) {
					buffer[i] += 'A' - 'a';
				}
			}


			//printf("Received: S%d: %s\n", sockfd, buffer);

			// Null-terminate the verb for strcmp
			buffer[4] = '\0';

			// Respond to each verb accordingly.
			if (STR_EQUAL(buffer, "HELO") || STR_EQUAL(buffer, "EHLO")) { // Initial greeting
				sprintf(bufferout, "250 Ok\r\n");
				printf("S%d: %s", sockfd, bufferout);
				send(sockfd, bufferout, strlen(bufferout), 0);
			}
			else if (STR_EQUAL(buffer, "MAIL")) { // New mail from...
				sprintf(bufferout, "250 Ok\r\n");
				printf("S%d: %s", sockfd, bufferout);
				send(sockfd, bufferout, strlen(bufferout), 0);
			}else if (STR_EQUAL(buffer, "TEST")) { // testing...
				sprintf(bufferout, "250 testing Ok\r\n");
				printf("S%d: %s", sockfd, bufferout);
				send(sockfd, bufferout, strlen(bufferout), 0);
			} else if (STR_EQUAL(buffer, "RCPT")) { // Mail addressed to...
				sprintf(bufferout, "250 Ok recipient\r\n");
				printf("S%d: %s", sockfd, bufferout);
				send(sockfd, bufferout, strlen(bufferout), 0);
			} else if (STR_EQUAL(buffer, "DATA")) { // Message contents...
				sprintf(bufferout, "354 Continue\r\n");
				printf("S%d: %s", sockfd, bufferout);
				send(sockfd, bufferout, strlen(bufferout), 0);
				indata = 1;
			} else if (STR_EQUAL(buffer, "RSET")) { // Reset the connection
				sprintf(bufferout, "250 Ok reset\r\n");
				printf("S%d: %s", sockfd, bufferout);
				send(sockfd, bufferout, strlen(bufferout), 0);
			} else if (STR_EQUAL(buffer, "NOOP")) { // Do nothing.
				sprintf(bufferout, "250 Ok noop\r\n");
				printf("S%d: %s", sockfd, bufferout);
				send(sockfd, bufferout, strlen(bufferout), 0);
			} else if (STR_EQUAL(buffer, "QUIT")) { // Close the connection
				sprintf(bufferout, "221 Ok\r\n");
				printf("S%d: %s", sockfd, bufferout);
				send(sockfd, bufferout, strlen(bufferout), 0);
				break;
			} else { // The verb used hasn't been implemented.
				sprintf(bufferout, "502 Command Not Implemented\r\n");
				printf("S%d: %s", sockfd, bufferout);
				send(sockfd, bufferout, strlen(bufferout), 0);
			}
		} else { // We are inside the message after a DATA verb.
			printf("C%d: %s\n", sockfd, buffer);

			if (STR_EQUAL(buffer, ".")) { // A single "." signifies the end
				sprintf(bufferout, "250 Ok\r\n");
				printf("S%d: %s", sockfd, bufferout);
				send(sockfd, bufferout, strlen(bufferout), 0);
				indata = 0;
			}
		}

		// Shift the rest of the buffer to the front
		memmove(buffer, eol+2, BUF_SIZE - (eol + 2 - buffer));
		buffer_offset -= (eol - buffer) + 2;

		// Do we already have additional lines to process? If so,
		// commit a horrid sin and goto the line processing section again.
		// TODO: change to loop while(strstr(buffer, "\r\n"))
		if (strstr(buffer, "\r\n"))
			goto processline;
	}

	// All done. Clean up everything and exit.
	close(sockfd);
	pthread_exit(NULL);
}

// Extract the address from sockaddr depending on which family of socket it is
void * get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
