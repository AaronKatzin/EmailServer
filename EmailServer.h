#ifndef EmailServer_H
#define EmailServer_H

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

#include "select.c"
//#include "smtp.c"
//#include "pop3.c"


#endif //#ifndef EmailServer_H
