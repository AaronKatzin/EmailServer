#include "EmailServer.h"


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
