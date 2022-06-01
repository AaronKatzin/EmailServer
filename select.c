
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
