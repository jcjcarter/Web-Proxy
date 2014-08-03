/* 
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Jayson Carter, jjc7@rice.edu 
 *     Eric Kang, ek8@rice.edu
 * 
 */ 

#include "csapp.h"

#define NITEMS 20  // number of items in shared buffer

/* shared variables */
int shared_buffer[NITEMS];  // holds file descriptors for connections
int count[NITEMS];
int shared_cnt;             // holds number of fd in shared_buffer
struct sockaddr_in addr;

/* set up lock/unlocks */
pthread_mutex_t mutex;
pthread_cond_t cond_empty;
pthread_cond_t cond_full;
unsigned int prod_index = 0;
unsigned int cons_index = 0;
unsigned int counter = 0;

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, int *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
			 char *uri, int size);
int open_listen(int port);
struct package *open_client(char *hostname, int port);

void doit(int fd, int index);
int read_requesthdrs(rio_t *rp, char *payload, int is1);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
int Rio_writen_w(int fd, void *usrbuf, size_t n);
ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n);

void *producer(void *argv);
void *consumer(void *arg);

struct package   
{
	int clientfd;
	struct sockaddr_in serveraddr;
};

struct package *
open_client(char *hostname, int port)
{
	int clientfd, error;
	struct addrinfo *ai;
	struct sockaddr_in serveraddr;
	struct package *package;

	package = malloc(sizeof(package));
    
	if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return NULL; /* check errno for cause of error */
    
	error = getaddrinfo(hostname, NULL, NULL, &ai);
    
	if (error != 0) {  // if an error is present, close everything and exit
		Close(clientfd);
		freeaddrinfo(ai);
		return NULL; 
	}

	/* Fill in the server's IP address and port */
	bzero((char *) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	bcopy(ai->ai_addr, 
	      (struct sockaddr *)&serveraddr, ai->ai_addrlen);
	serveraddr.sin_port = htons(port);
    
	/* Establish a connection with the server */
	if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0) {
		return NULL;
	}
    
	freeaddrinfo(ai);

	package->clientfd = clientfd;
	package->serveraddr = serveraddr;

	return package;
}


/* 
 * main - Main routine for the proxy program 
 */
int
main(int argc, char **argv)
{

	//pthread_t prod_tid;
	pthread_t cons_tid1, cons_tid2,  cons_tid3, cons_tid4, cons_tid5, cons_tid6, cons_tid7, cons_tid8;

	/* Check the arguments. */
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
		exit(0);
	}

	Signal(SIGPIPE, SIG_IGN);  // ignore sig_pipe

	/* Initialize pthread variables */
	Pthread_mutex_init(&mutex, NULL);
	Pthread_cond_init(&cond_full, NULL);
	Pthread_cond_init(&cond_empty, NULL);

	/* Start consumer threads */
	Pthread_create(&cons_tid1, NULL, consumer, NULL);	
	Pthread_create(&cons_tid2, NULL, consumer, NULL);
	Pthread_create(&cons_tid3, NULL, consumer, NULL);
	Pthread_create(&cons_tid4, NULL, consumer, NULL);
	Pthread_create(&cons_tid5, NULL, consumer, NULL);
	Pthread_create(&cons_tid6, NULL, consumer, NULL);
	Pthread_create(&cons_tid7, NULL, consumer, NULL);
	Pthread_create(&cons_tid8, NULL, consumer, NULL);
		
	/* Start producer thread */
	producer((void *)argv);
	return (0);

}


void *
producer(void *argv) 
{
	int connfd, port, listenfd, error;
	socklen_t clientlen;
	struct sockaddr_in clientaddr;
	char host_name[NI_MAXHOST]; 
	char haddrp[INET_ADDRSTRLEN];

	char **argv1 = (char **)argv;

	port = atoi(argv1[1]);         /*Get the port number from the input.*/
	listenfd = open_listen(port);  // receive request from client
	if (listenfd < 0) {
		unix_error("open_listen error");
	}

  
	while (1) {
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // make connection with client

		Pthread_mutex_lock(&mutex);   // acquire mutex lock

		/* determine the domain name and IP address of the client */
		error = getnameinfo((struct sockaddr *)&clientaddr, sizeof(clientaddr), 
				    host_name, sizeof(host_name), NULL, 0, 0);
		if (error != 0) {
			close(connfd);
			continue;
		}
		inet_ntop(AF_INET, &clientaddr.sin_addr, haddrp, INET_ADDRSTRLEN);
      		printf ("Request %d: Received request from %s (%s)\n", counter, host_name, haddrp);

		while (shared_cnt == NITEMS) {
			Pthread_cond_wait(&cond_full, &mutex);  // if buffer full, wait till signaled
		}

		shared_buffer[prod_index] = connfd;  // store file descriptor in shared buffer
		count[prod_index] = counter++;

		if (shared_cnt == 0) {
			Pthread_cond_broadcast(&cond_empty);  // signal only if shared buffer is empty
		}
		shared_cnt++;

		if (prod_index == NITEMS-1)
			prod_index = 0;
		else
			prod_index++;

		Pthread_mutex_unlock(&mutex); // release the lock
	}

	return NULL;
}


void *
consumer(void *arg)
{
	int connfd;

	Pthread_detach(pthread_self());  
	arg = (void *)arg;

	while (1) {
		Pthread_mutex_lock(&mutex); // acquire mutex lock
		//	printf("thread = %d\n", (int)pthread_self());

		while (shared_cnt == 0) {
			Pthread_cond_wait(&cond_empty, &mutex);  // if buffer empty, wait till something added
		}

		connfd = shared_buffer[cons_index];  // read file descriptor from shared buffer
		
		doit(connfd, cons_index);	// main processing
		Close(connfd);  // close connection

		if (shared_cnt == NITEMS) {
			Pthread_cond_signal(&cond_full);  // signal only if buffer was full
		}

		shared_cnt--;

		if(cons_index == NITEMS-1)
			cons_index = 0;
		else
			cons_index++;

		Pthread_mutex_unlock(&mutex);  // unlock

	}
	return NULL;
}

/*  
 * open_listenfd - open and return a listening socket on port
 *     Returns -1 and sets errno on Unix error.
 */
int 
open_listen(int port) 
{
	int listenfd, optval=1;
	struct sockaddr_in serveraddr;
  
	/* Create a socket descriptor */
	if ((listenfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) 
		return -1;
 
	/* Eliminates "Address already in use" error from bind. */
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
		       (const void *)&optval , sizeof(int)) < 0) 
		return -1;

	/* Listenfd will be an endpoint for all requests to port
	   on any IP address for this host */
	bzero((char *) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET; 
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
	serveraddr.sin_port = htons((unsigned short)port); 
	if (bind(listenfd, (SA *)&serveraddr, sizeof(serveraddr)) < 0) 
		return -1;

	/* Make it a listening socket ready to accept connection requests */
	if (listen(listenfd, LISTENQ) < 0) 
		return -1;

	return listenfd;
}

/*
 * parse_uri - URI parser
 * 
 * Requires: 
 *   The memory for hostname and pathname must already be allocated
 *   and should be at least MAXLINE bytes.  Port must point to a
 *   single integer that has already been allocated.
 *
 * Effects:
 *   Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 *   the host name, path name, and port.  Return -1 if there are any
 *   problems and 0 otherwise.
 */

int 
parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
	char *hostbegin;
	char *hostend;
	int len, i, j;
	printf("**********8This is the uri %s\n", uri);
	if (strncasecmp(uri, "http://", 7) != 0) {
		hostname[0] = '\0';
		return (-1);
	}
	   
	// Extract the host name.
printf("\n");
	printf("**********uri plus seven %s\n", uri +7);
	printf("\n");
	hostbegin = uri + 7;
	printf("***********hostbegin %s\n", hostbegin);
printf("\n");
	hostend = strpbrk(hostbegin, " :/\r\n");
	printf("***********hostend %s\n", hostend);
printf("\n");
	if (hostend == NULL)
		hostend = hostbegin + strlen(hostbegin);
	len = hostend - hostbegin;
	strncpy(hostname, hostbegin, len);
	hostname[len] = '\0';

	// Look for a port number.  If none is found, use port 80. 
	*port = 80;
	if (*hostend == ':')
		*port = atoi(hostend + 1);
	
	// Extract the path. 
	for (i = 0; hostbegin[i] != '/'; i++) {
		if (hostbegin[i] == ' ') 
			break;
	}
	if (hostbegin[i] == ' ') {
		strcpy(pathname, "/");
	}
	else {
		for (j = 0; hostbegin[i] != ' '; j++, i++) { 
			pathname[j] = hostbegin[i];
		}
		pathname[j] = '\0';
	}

	return (0);
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 *
 * Requires:
 *   The memory for logstring must already be allocated and should be
 *   at least MAXLINE bytes.  Sockaddr must point to an allocated
 *   sockaddr_in structure.  Uri must point to a properly terminated
 *   string.
 *
 * Effects:
 *   A properly formatted log entry is stored in logstring using the
 *   socket address of the requesting client (sockaddr), the URI from
 *   the request (uri), and the size in bytes of the response from the
 *   server (size).
 */
void
format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri,
		 int size)
{
	time_t now;
	char time_str[MAXLINE];
	unsigned long host;
	unsigned char a, b, c, d;

	/* Get a formatted time string. */
	now = time(NULL);
	strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z",
		 localtime(&now));

	/*
	 * Convert the IP address in network byte order to dotted decimal
	 * form.  Note that we could have used inet_ntoa, but chose not to
	 * because inet_ntoa is a Class 3 thread unsafe function that
	 * returns a pointer to a static variable (Ch 13, CS:APP).
	 */
	host = ntohl(sockaddr->sin_addr.s_addr);
	a = host >> 24;
	b = (host >> 16) & 0xff;
	c = (host >> 8) & 0xff;
	d = host & 0xff;

	/* Return the formatted log entry string */
	sprintf(logstring, "%s: %d.%d.%d.%d %s %d", time_str, a, b, c, d, uri,
		size);
	
}


/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd, int index) 
{
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	rio_t rio;
	int clientfd;

       	char formatted_request[MAXLINE]; 
	char payload[10 * MAXLINE];

       	char pathname[MAXLINE];
	char hostname[MAXLINE];
	int port;

	int is1 = 0;   // is http 1.1? 0 = no, 1 = yes

	char logger[MAXLINE];
	FILE *fp;

	struct package *package;
	struct sockaddr_in serveraddr;

	/* Read request line and headers */
       	Rio_readinitb(&rio, fd);
	if(Rio_readlineb(&rio, buf, MAXLINE) <= 0) {  // reads request header from client
		return ;
	}

	sscanf(buf, "%s %s %s", method, uri, version);  // splits request header

	if (strstr(version, "1.1")) {  // checks to see which version of http
		is1 = 1;
	}

	if (strcasecmp(method, "GET")) {  // is the method GET? We don't do any non-GET requests
		clienterror(fd, method, "501", "Not Implemented",
			    "process_request: Received non-GET request");
		return;
	}
        if (parse_uri(uri, &hostname[0], &pathname[0], &port) != 0) {  // extracts hostname, path, and port
		return ;
	}

       	printf("%s", buf);
       	printf("Host: %s\n", hostname);

	strcat(payload, "Host: ");  // modify request header to include "Host: hostname" as first line
	strcat(payload, hostname);
	strcat(payload, "\r\n");

	if (read_requesthdrs(&rio, payload, is1) < 0) {  // take in the rest of the request headers
		return ;
	}

       	printf("\n*** End of Request ***\n\n");

       	printf("Stripping header Proxy Connection\n");
       	sprintf(formatted_request, "%s %s %s\r\n", method, pathname, version);  // before sending to server, we format it to what server wants
	
       	printf("Request %d: Forwarding request to end server:\n", count[index]);
	//    	printf("%s", formatted_request);
	//	printf("%s\n", payload);

       	printf("\n*** End of Request ***\n\n");

	if ((package = open_client(&hostname[0], port)) == NULL) {  // open communications with server
		free(package);
		return ;
	}

	clientfd = package->clientfd;
	if (clientfd < 0) {
		free(package);
		return;
	}
	serveraddr = package->serveraddr;
	free(package);

	if (Rio_writen_w(clientfd, formatted_request, strlen(formatted_request)) < 0) {  // send request header to server
		Close(clientfd);
		return ;
	}
	
	if(Rio_writen_w(clientfd, payload, strlen(payload)) < 0) {  // send rest of header (payload) to server
		Close(clientfd);
		return ;
	}

    	Rio_readinitb(&rio, clientfd);

	char temp[MAXLINE];                        // read and send payload and header to client
	int nread;
	int cum_sum = 0;
	while ((nread = Rio_readnb_w(&rio, temp, MAXLINE)) > 0) {  // reads in MAXLINE bytes at a time
		//	printf("%s\n", temp);
		cum_sum += nread;
		if (Rio_writen_w(fd, temp, nread) < 0) {
			printf("Cannot send payload to client\n");
			Close(clientfd);
			return ;
		}
	}
       	printf("Request %d: Forwarded %d bytes from end server to client\n", count[index], cum_sum);

	format_log_entry(logger, &serveraddr, uri, cum_sum);  // log the connection
	fp = fopen("proxy.log", "a");
	fprintf(fp, "%s\n", logger);
	fclose(fp);

	Close(clientfd);
	return ;
}

/*
 * read_requesthdrs - read and parse HTTP request headers
 */
int read_requesthdrs(rio_t *rp, char *payload, int is1) 
{
	char buf[MAXLINE];
	if (Rio_readlineb(rp, buf, MAXLINE) <= 0) {
		return -1;
	}
	while(strcmp(buf, "\r\n")) {
		if (Rio_readlineb(rp, buf, MAXLINE) <= 0) 
			return -1;
		
		//	printf("%s", buf);
	       	if(strstr(buf, "Proxy-Connection"))  // strip off proxy connection
			continue;
		
		if(strstr(buf, "Connection"))  // strip off connection, we don't support persistent connections
			continue;
		else 
			strcat(payload, buf);  
		
	}
	if (is1) {
		payload[strlen(payload)-2] = '\0';
		strcat(payload, "Connection: close\r\n\r\n");  // always add this line to end of headers to close connection
	}
	return 0;
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
	char buf[MAXLINE], body[MAXBUF];

	/* Build the HTTP response body */
	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

	/* Print the HTTP response */
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, body, strlen(body));

	printf("process_request: client issued a bad request (1)\n");
}

/*
 *  Wrappers for Rio_readn, Rio_readlineb, Rio_readnb,  and Rio_writen
 */

ssize_t 
Rio_readn_w(int fd, void *ptr, size_t nbytes) 
{
	ssize_t n;

	if ((n = rio_readn(fd, ptr, nbytes)) < 0) {
		fprintf(stdout, "Rio_readn_w error\n");
		return -1;
	}
	return n;
}

ssize_t 
Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen)
{
	ssize_t rc;
	if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
		fprintf(stdout, "Rio_readlineb_w error\n");
		return -1;
	}
	return rc;
}

int
Rio_writen_w(int fd, void *usrbuf, size_t n) 
{
	if (rio_writen(fd, usrbuf, n) != (unsigned int) n) {
		fprintf(stdout, "Rio_writen_w error\n");
		return -1;
	}
	return 0;
}

ssize_t
Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n) 
{
	ssize_t rc;
	if((rc = rio_readnb(rp, usrbuf, n)) < 0) {
		printf(".....\n");
		fprintf(stdout, "Rio_readnb_w error\n");
		return -1;
	}
	return rc;
}

/*
 * The last lines of this file configures the behavior of the "Tab" key in
 * emacs.  Emacs has a rudimentary understanding of C syntax and style.  In
 * particular, depressing the "Tab" key once at the start of a new line will
 * insert as many tabs and/or spaces as are needed for proper indentation.
 */

/* Local Variables: */
/* mode: c */
/* c-default-style: "bsd" */
/* c-basic-offset: 8 */
/* c-continued-statement-offset: 4 */
/* indent-tabs-mode: t */
/* End: */
