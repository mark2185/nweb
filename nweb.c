#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sysexits.h>
#include <signal.h>

#define MAX_CLIENTS 10
#define BUFSIZE 8096 

struct {
	char *ext;
	char *filetype;
} extensions [] = {
	{ "gif",  "image/gif" },
    { "jpg",  "image/jpeg" },
    { "jpeg", "image/jpeg" },
    { "png",  "image/png" },
    { "zip",  "image/zip" },
    { "gz",   "image/gz" },
    { "tar",  "image/tar" },
    { "htm",  "text/html" },
    { "html", "text/html" },
    { 0, 0 }
};

static const char not_found_response[] = 
				"HTTP/1.1 404 Not Found\r\n"
				"Content-Type: text/html\r\n"
				"\r\n"
				"<HTML>\r\n"
				" <BODY>\r\n"
				"  <h1>Not Found</h1>\r\n"
				"  <p>The requested URL was not found on this server.</p>\r\n"
				" </BODY>\r\n"
				"<HTML>\r\n";	

static const char forbidden_response[] = 
				"HTTP/1.1 403 Forbidden\r\n"
				"Content-Type: text/html\r\n"
				"\r\n"
				"<HTML>\r\n"
				" <BODY>\r\n"
				"  <h1>Access denied</h1>\r\n"
				"  <p>I'm sorry Dave, I cannot let you do that.</p>\r\n"
				" </BODY>\r\n"
				"<HTML>\r\n";	

static const char get_root_response_header[] =
				"HTTP/1.1 200 OK\r\n"
				"Content-Type: text/html\r\n"
				"\r\n"
				"<HTML>\r\n"
				" <TITLE>Mali web server</TITLE>\r\n"
				" <BODY>\r\n"
				" <h3>Neki folder</h3>\r\n"
				" <p>\r\n";

void print_usage()
{
	fprintf(stderr, "./nweb [tcp_port]\n");
	exit(1);
}

void get_index(int *socket)
{
		DIR *directory;
		struct dirent *dp;
		directory = opendir(".");
		char HTML[BUFSIZE + 1];
		
//		printf("Requested index\n");
		send(*socket, get_root_response_header, strlen(get_root_response_header), 0);
		while((dp = readdir(directory)) != NULL) {
			struct stat file_stat;
			if (!strncmp(dp->d_name, ".", 1) || !strncmp(dp->d_name, "..", 2))
				continue;
			if (stat(dp->d_name, &file_stat)) 
				continue;

			sprintf(HTML, "<a href=\"%s\">%s</a> (%d)<br>\r\n", dp->d_name, dp->d_name, (int)file_stat.st_size);

			send(*socket, HTML, strlen(HTML), 0);
		}
		sprintf(HTML, "<p></BODY></HTML>\r\n");
		send(*socket, HTML, strlen(HTML), 0);
		
		// pitanje - slat sve odjednom ili je okej ovako cijepat? ^

//		printf("Thread %u exited\n", (unsigned int) pthread_self());
}

void get_requested_file(int *socket, char *file, char *filetype) 
{
	FILE *fd;
	char buf[BUFSIZE + 1];
	int sentbytes=0, readbytes, size;
	char get_response[BUFSIZE + 1];

	fd = fopen(file+1, "r");
	if (fd == NULL) {
		send(*socket, not_found_response, strlen(not_found_response), 0);
		return;
	}

	fseek(fd, 0, SEEK_END);
	size = ftell(fd);
	fseek(fd, 0, SEEK_SET);

	// pitanje - jel okej ovako provjeravat ili bih trebao s writeom? ^ 
//	printf("Starting the transport...\n");
	sprintf(get_response, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nContent-Type: %s\r\n\r\n", size, filetype);
	send(*socket, get_response, strlen(get_response), 0);

	while (sentbytes < size) {
		readbytes = fread(buf, 1, 1024, fd);
		sentbytes += send(*socket, buf, readbytes, 0);
	}
//	printf("File sent\n");

}

void *web(void *parameters)
{
	int *socket = parameters;
	int read_bytes;

	char *requested_file, *ext;
	char buffer[BUFSIZE + 1];

	int addrlen;
	struct sockaddr_in address;

//	printf("Thread %u started\n", (unsigned int) pthread_self());

	read_bytes = read(*socket, buffer, BUFSIZE);

	getpeername(*socket, (struct sockaddr*)&address, (socklen_t*)&addrlen);

	if (read_bytes == 0 || read_bytes == -1) {
		printf("Random empty msg from %s!\n", inet_ntoa(address.sin_addr));
		close(*socket);
//		printf("Thread %u exited\n", (unsigned int) pthread_self());
		return NULL;
	}

	buffer[read_bytes] = 0;

//	printf("Original request: %s\n", buffer);
	for (int i=0; i<read_bytes; i++)
		if (buffer[i] == '\r' || buffer[i] == '\n') {
			buffer[i] = 0;
			break;
		}
//	puts(buffer);
	printf("%s: %s\n", inet_ntoa(address.sin_addr), buffer);

	if (strncmp("GET ", buffer, 4) && strncmp("get ", buffer, 4)) {
		send(*socket, "HTTP/1.1 405\r\n\r\n", 18, 0);
		printf("Only simple requests, exiting thread %u\n", (unsigned int) pthread_self());
		close(*socket);
//		printf("Thread %u exited\n", (unsigned int) pthread_self());
		return NULL;
	}
	
	for(int i=4; i<read_bytes; i++) {
		if (buffer[i] == ' ') {
			buffer[i]=0;
			break;
		}
	}
	requested_file = &buffer[4];

	if (!strncmp(requested_file, "/\0", 3) || !strncmp(requested_file, "/index.html\0", 13)) {
		get_index(socket);
		close(*socket);
		return NULL;
	}

	if (!strncmp(requested_file, "..", 2)) {
		//send(*socket, "403 HTTP/1.1\r\n", 18, 0);
		send(*socket, forbidden_response, strlen(forbidden_response), 0);
//		printf("Other folders not accessible\n");
		close(*socket);
		return NULL;
	}

	if ((ext = strrchr(requested_file, '.')) != NULL) {
		ext += 1;
	} else {
		printf("Extension not supported!\n");
		//send(*socket, "403 HTTP/1.1\r\n\r\n", 18, 0);
		send(*socket, forbidden_response, strlen(forbidden_response), 0);
		close(*socket);
		return NULL;
	}
	
	//printf("In string %s, the last position of the . is %d, and the extension is \"%s\"\n", buffer, ext-buffer+1, ext);

	int file_flag=0;
	for(int i=0; extensions[i].ext != 0; i++) {
		if (strlen(extensions[i].ext) != strlen(ext))
			continue;

		//printf("Comparing \"%s\" to \"%s\"\n", ext, extensions[i].ext);
		if (!strncmp(ext, extensions[i].ext, strlen(ext))) {
			file_flag = i+1;
			break;
		}
	}

	if (!file_flag) {
		printf("Extension not supported!\n");
		send(*socket, forbidden_response, strlen(forbidden_response), 0);
	} else {
		get_requested_file(socket, requested_file, extensions[file_flag-1].filetype);
		close(*socket);
	}

	close(*socket);
//	printf("Thread %u exited\n", (unsigned int) pthread_self());
	return NULL;
}

int create_master_socket(int *sock, char *port)
{
	int opt;
	struct sockaddr_in address;

	*sock = socket(AF_INET, SOCK_STREAM, 0);

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(atoi(port));

	if (setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0 ) 
		errx(1, "Error setting master socket options!\n");

	return bind(*sock, (struct sockaddr *)&address, sizeof(address));
}

int master_socket;
void cleanup()
{
	printf("Closing the master socket!\n");
	close(master_socket);
	exit(0);
}


int main(int argc, char *argv[])
{
//	int opt;//, error=0;
	//int master_socket, client_socket;
	int client_socket;

	struct sockaddr_in address;
	int addrlen;
	char *port = "80";

	pthread_t tid;

	signal(SIGINT, cleanup);

	if (argc > 2)
		print_usage();
	else if(argc == 2)
		port = argv[1];
	
	if (create_master_socket(&master_socket, port) < 0) {
//		fprintf(stderr, "Error binding master socket!\n%s\n", strerror(errno));
		errx(EX_UNAVAILABLE, "Error binding master socket!\n");
//		return 1;
	}
	
	if (listen(master_socket, MAX_CLIENTS) < 0)
		errx(EX_UNAVAILABLE, "Error listening for connections!\n");

	printf("Webserver started on port %s...\n", port);
	while(1) {
		if ((client_socket = accept(master_socket, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0)
			errx(EX_UNAVAILABLE, "Error accepting a client!\n");

		pthread_create(&tid, NULL, &web, (void *)&client_socket);
	}
	return 0;
}
