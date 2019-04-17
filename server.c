#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h> 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <signal.h>
#include <sys/sendfile.h>
#define LOCAL_PORT 80
#define BACK_LOG 20
#define N_ELEMS(x)  (sizeof(x) / sizeof((x)[0]))

typedef struct {
	char *method;
	char *resource;
	char *body;
} request;

void cleanup(FILE*);
int isSpace(char);
char *getContentType(char*);
char *concat(const char*, const char*);
void print404(char*);
void print500(char*); 
void directoryListing(char*);
request *parseRequest(char *);
int readFile(struct stat *, char*);
void handleGet(request *);

struct { 
	char *ext;
	char *filetype;
} extensions[] = {
	{".html", "text/html"},
	{".txt", "text/plain"},
	{".png", "image/png"},
	{".svg", "image/svg"},
	{".xml", "application/xml"},
	{".xsl", "application/xslt+xml"},
	{".css", "text/css"},
	{".js", "application/javascript"},
	{".json", "application/json"}
};

struct sigaction old_action;

int main () {
	// Handle Zombie Processes
	signal(SIGCHLD, SIG_IGN);

	FILE *erfd = fopen("/var/webserver/error.log", "a");
	// Redirect stderr to file.
	dup2(fileno(erfd), 2);

	struct sockaddr_in  local_addr;
	int sd, new_sd;
	sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));

	local_addr.sin_family      = AF_INET;
	local_addr.sin_port        = htons((uint16_t)LOCAL_PORT); 
	local_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if ( !bind(sd, (struct sockaddr *)&local_addr, sizeof(local_addr)) )
		fprintf(stderr, "Prosess %d er knyttet til port %d.\n", getpid(), LOCAL_PORT);
	else {
		perror("could not bind to address");
		if(erfd != NULL)
			fclose(erfd);
	
		exit(1);
	}

	chroot("/var/www");
	int gidRes = setgid(1001);
	int uidRes = setuid(1001);
	if(uidRes == -1 || gidRes == -1) {
		perror("could not set uid and gid: ");
		if(erfd != NULL)
			fclose(erfd);
	
		exit(1);
	}

	listen(sd, BACK_LOG); 
	while(1){ 
		new_sd = accept(sd, NULL, NULL);    
		if(!fork()) {
			dup2(new_sd, 1); // redirect socket to stdout and stdin
			dup2(new_sd, 0); 

			char buf[BUFSIZ];
			recv(0, buf, BUFSIZ, 0);
			request* newRequest = parseRequest(buf);

			// If parsing failed shutdown and exit
			if(newRequest == NULL) {
				cleanup(erfd);
				exit(1);
			} 

			if(!strcmp(newRequest->method, "GET")) {
				handleGet(newRequest);
			}

			// cleanup and exit
			free(newRequest);
			cleanup(erfd);
			exit(0);
		} else {
			close(new_sd);
		}
	}
	return 0;
}

request *parseRequest(char *buf) {
	request *newRequest = malloc(sizeof(request));
	const char *start = buf;
	const char *end;

	if(!strncmp("GET", start, 3)){
		newRequest->method = "GET";
		// Jump past: "GET "
		start += 4;
	} else {
		printf("HTTP/1.1 405 Method Not Allowed\nContent-Type: text/plain\n\nServer does not support that method.");
		free(newRequest);
		return NULL;
	}

	end=start;
	while(*end && !isSpace(*end))
		++end;

	size_t pathLen = (end - start);
	char *path = malloc(pathLen + 1);

	if(path == NULL) {
		print500("Internal Server Error.");
		perror("Malloc Error: ");
		free(newRequest);
		return NULL;
	}
	memcpy(path, start, pathLen);
	path[pathLen] = '\0';

	if (pathLen > 1 && path[pathLen - 1] == '/')
		path[pathLen -1] = '\0';

	newRequest->resource = path;
	return newRequest;
}


void handleGet(request *newRequest) {
	struct stat path_stat;
	int res = stat(newRequest->resource, &path_stat);
	if (res == -1) {
		print404("File or Directory not found");
		perror("HandleGet Error: ");
		return;
	} else if(!S_ISREG(path_stat.st_mode) && !S_ISDIR(path_stat.st_mode)) {
		print404("File or Directory not found");
		perror("HandleGet Error: ");
	} else if(S_ISREG(path_stat.st_mode)) { // is file
		readFile(&path_stat, newRequest->resource);
	} else if(S_ISDIR(path_stat.st_mode)) {
		directoryListing(newRequest->resource);
	}
}


void directoryListing(char *dirPath){
	struct stat       stat_buffer;
	struct dirent    *ent;
	DIR              *dir;

	if ((dir = opendir(dirPath)) == NULL) {
		perror("error opening directory"); 
		exit(1);
	}

	printf("HTTP/1.1 200 OK\nContent-Type: text/html\n\n");
	printf("<html><head></head><body><h1>Index of %s</h1><table cellspacing=\"10\"><tr><th>Name</th><th>Mode</th><th>User ID</th><th>Group ID</th><th>Size</th></tr>", dirPath);

	while ((ent = readdir(dir)) != NULL) {
		char *filePath;
		if(!strcmp(dirPath, "/")) {
			// add root path add "/" infront of name.
			filePath = concat("/", ent->d_name);
		} else {
			filePath = concat(dirPath, concat("/", ent->d_name));
		}

		if (stat(filePath, &stat_buffer) < 0) {
			perror("Error:"); 
			exit(2); 
		}

		printf ("<tr><td><a href=\"%s\">%s</a></td>", filePath, ent->d_name);
		printf ("<td>%o</td>", stat_buffer.st_mode & 0777 );      
		printf ("<td>%d</td>", stat_buffer.st_uid);
		printf ("<td>%d</td>", stat_buffer.st_gid);
		printf ("<td>%ld</td></tr>",   stat_buffer.st_size);
	}

	printf("</table></body></html>");
	closedir(dir);
}

int readFile(struct stat *path_stats, char* path) {
	FILE *fd;
	if ((fd = fopen(path,"r")) == NULL){
		print500("Could not open file.");
		perror("Error: ");
		return -1;
	}

	char *contentType = getContentType(path);
	if (contentType == NULL) {
		printf("HTTP/1.1 415 Unsupported Media Type\nContent-Type: text/plain\n\nFile type not supported");
		if(fd != NULL)
			fclose(fd);
		
		return 1;
	}

	// Write header
	char buf[BUFSIZ];
	sprintf(buf, "HTTP/1.1 200 OK\nContent-Length: %ld\nContent-Type: %s\nServer: Spacy\n\n", path_stats->st_size, contentType);
	send(1, buf, strlen(buf), 0);

	// Read file and Write to body
	int readRet;
	while((readRet = fread(buf, 1, BUFSIZ, fd)) > 0)
		send(1, buf, readRet, 0);

	if(fd != NULL) 
		fclose(fd);

	return 1;
}

char* getContentType(char *path) {
	char *contentType = NULL;
	char *ext = strrchr(path, '.');
	int length = N_ELEMS(extensions);
	for(int i = 0; i < length; i++) {
		if(!strcmp(ext, extensions[i].ext)) {
			contentType = extensions[i].filetype;
			break;
		}
	}

	return contentType;
}

void cleanup(FILE *erfd) {
	fflush(stdout);	
	if(erfd != NULL)
		fclose(erfd);
	shutdown(1, SHUT_RDWR); 
}

int isSpace(char c) {
	return c == ' ' ? 1 : 0;
}

char* concat(const char *s1, const char *s2) {
	char *result = malloc(strlen(s1) + strlen(s2) + 1); // +1 for the null terminator
	strcpy(result, s1);
	strcat(result, s2);
	return result;
}

void print404(char *message) {
	printf("HTTP/1.1 404 Not Found\nContent-Type: text/plain\n\n%s\n", message);
}

void print500(char *message) {
	printf("HTTP/1.1 500 Internal Server Error\nContent-Type: text/plain\n\n%s\n", message);
}

