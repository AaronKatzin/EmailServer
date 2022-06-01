#include "smtp.c"
#include "pop3.c"


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
