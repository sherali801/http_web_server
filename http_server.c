#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern int errno;

#define BUFFER_SIZE 4096
#define MAX_REQUEST_ITEMS 3
#define REQUEST_ITEM_LENGTH 128
#define DEFAULT_PORT 50000

#define OK 200
#define METHOD_NOT_ALLOWED 405
#define NOT_FOUND 404
#define UNSUPPORTED_MEDIA_TYPE 415
#define INTERNAL_SERVER_ERROR 500

#define DOCUMENT_PATH "/var/www/htdocs/"
#define RESPONSES_PATH "/var/www/responses/"
#define LOG_PATH "/var/www/logs/"

struct {
	char * extension;
	char * content_type;
} extensions [] = {
	{"gif", "image/gif" },
	{"jpg", "image/jpeg"},
	{"jpeg","image/jpeg"},
	{"png", "image/png" },
	{"zip", "image/zip" },
	{"gz",  "image/gz"  },
	{"tar", "image/tar" },
	{"htm", "text/html" },
	{"html","text/html" },
	{"cgi", "text/cgi"  },
	{"xml", "text/xml"  },
	{"js",  "text/js"   },
	{"css", "text/css"  },    
    {0,0}
};

void access_log(char ** request_items_list);
void log_error(char * error);
void exit_with_error(char * error_message);
int is_number_of_arguments_valid(int number_of_arguments);
int convert_to_integer(char * str);
int is_port_valid(int port);
int is_port_given(int args);
int get_server_socket();
struct sockaddr_in get_server_address(int port);
int bind_server_socket_with_server_address(int server_socket, struct sockaddr_in server_address);
int listen_on_server_socket(int server_socket);
void reap_child_process();
int get_client_socket(int server_socket, struct sockaddr_in * client_address);
int accept_client_connection_request(int server_socket);
char * get_client_request(int client_socket);
char ** tokenize_request(char * request);
int is_valid_request_type(char * request_type);
void send_response(int client_socket, char * send_buffer, int count);
char * get_header(int code);
void send_header(int client_socket, int code, char * content_type);
int is_root_directory(char * path);
int is_directory(char * path);
void get_total_entries_and_max_size_in_directory(char * path, int * total_entries, int * max_size);
int cmpstringp(const void *p1, const void *p2);
void send_directory_listing(int client_socket, char * path);
int is_regular_file(char * path);
char * is_requested_file_type_valid(char * file_name);
void send_file(int client_socket, char * path);
int close_file_descriptor(int file_descriptor);
int shutdown_connection(int socket);
void send_internal_server_error(int client_socket);

int main(int argc, char * argv[])
{
	if (is_number_of_arguments_valid(argc) == -1) {
		exit_with_error("Usage: ./http_server <port>(49152-65535).");
	}
	int port = DEFAULT_PORT;
	if (is_port_given(argc) == 0){
		port = convert_to_integer(argv[1]);
	}
	if (is_port_valid(port) == -1) {
		exit_with_error("Error: Port must be between 49152 and 65535.");
	}
	int server_socket = get_server_socket();
	if (server_socket == -1) {
		exit_with_error("Error: Can't create server socket.");
	}
	struct sockaddr_in server_address = get_server_address(port);
	if (bind_server_socket_with_server_address(server_socket, server_address) == -1) {
		exit_with_error("Error: Can't bind server socket with server address.");
	}
	if (listen_on_server_socket(server_socket) == -1) {
		exit_with_error("Error: Can't listen on server socket.");
	}
	printf("Listening on port %d\n", port);
	reap_child_process();
	int client_socket = accept_client_connection_request(server_socket);
	char * request = get_client_request(client_socket);
	char ** request_items_list = tokenize_request(request);
	access_log(request_items_list);
	char * path = NULL;
	char real_path[BUFFER_SIZE];
	char * header = NULL;
    char * content_type = NULL;
	if (is_valid_request_type(request_items_list[0]) == -1) {
        path = "405.html";
        content_type = "text/html";
        snprintf(real_path, BUFFER_SIZE, "%s%s", RESPONSES_PATH, path);
		send_header(client_socket, METHOD_NOT_ALLOWED, content_type);
		send_file(client_socket, real_path);
	} else {
		strncpy(real_path, DOCUMENT_PATH, BUFFER_SIZE);
		path = ++request_items_list[1];
		strcat(real_path, path);
        if (is_root_directory(real_path) == 0) {
            content_type = "text/html";
			send_header(client_socket, OK, content_type);
            send_directory_listing(client_socket, real_path);
        } else if (is_directory(real_path) == 0) {
            content_type = "text/html";
			send_header(client_socket, OK, content_type);
			send_directory_listing(client_socket, real_path);
		} else if (is_regular_file(real_path) == 0) {
			if ((content_type = is_requested_file_type_valid(real_path)) != NULL) {
				send_header(client_socket, OK, content_type);
				send_file(client_socket, real_path);
			} else {
                path = "415.html";
                content_type = "text/html";
                snprintf(real_path, BUFFER_SIZE, "%s%s", RESPONSES_PATH, path);
				send_header(client_socket, UNSUPPORTED_MEDIA_TYPE, content_type);
				send_file(client_socket, real_path);
			}
		} else {
            path = "404.html";
            content_type = "text/html";
            snprintf(real_path, BUFFER_SIZE, "%s%s", RESPONSES_PATH, path);
			send_header(client_socket, NOT_FOUND, content_type);
			send_file(client_socket, real_path);
		}
	}
	if (shutdown_connection(client_socket) == -1) {
		exit_with_error("Error: Can't close the connection.");
	}
	if (close_file_descriptor(client_socket) == -1) {
		exit_with_error("Error: Can't close client socket.");
	}
	return 0;
}

void access_log(char ** request_items_list) {
	time_t time_in_seconds = time(NULL);
	char * timestamp = ctime(&time_in_seconds);
	timestamp[strlen(timestamp) - 1] = '\0';
	char buffer[BUFFER_SIZE];
	snprintf(buffer, BUFFER_SIZE, "[%s] - %s, %s, %s\n", timestamp, request_items_list[0], request_items_list[1], request_items_list[2]);
	char * path = "access.log";
    char real_path[BUFFER_SIZE];
    snprintf(real_path, BUFFER_SIZE, "%s%s", LOG_PATH, path);
    int fd = open(real_path, O_CREAT | O_WRONLY | O_APPEND, 0664);
	write(fd, buffer, strlen(buffer));
    close_file_descriptor(fd);
}

void log_error(char * error_message) {
	time_t time_in_seconds = time(NULL);
	char * timestamp = ctime(&time_in_seconds);
	timestamp[strlen(timestamp) - 1] = '\0';
	char error[BUFFER_SIZE];
	snprintf(error, BUFFER_SIZE, "[%s] - %s\n", timestamp, error_message);
    char * path = "error.log";
    char real_path[BUFFER_SIZE];
    snprintf(real_path, BUFFER_SIZE, "%s%s", LOG_PATH, path);
	int fd = open(real_path, O_CREAT | O_WRONLY | O_APPEND, 0664);
	write(fd, error, strlen(error));
    close_file_descriptor(fd);
}

void exit_with_error(char * error_message) {
	fprintf(stderr, "%s\n", error_message);
	log_error(error_message);
	exit(-1);
}

int is_number_of_arguments_valid(int number_of_arguments) {
	return number_of_arguments <= 2 ? 0 : -1;
}

int convert_to_integer(char * str) {
	return atoi(str);
}

int is_port_valid(int port) {
	return (port >= 49152 && port <= 65535) ? 0 : -1;
}

int is_port_given(int args){
	return args == 2  ? 0 : -1;
}

int get_server_socket() {
    return socket(AF_INET, SOCK_STREAM, 0);
}

struct sockaddr_in get_server_address(int port) {
	struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    memset(&(server_address.sin_zero), '\0', sizeof(server_address.sin_zero));
    server_address.sin_addr.s_addr = INADDR_ANY;
    return server_address;
}

int bind_server_socket_with_server_address(int server_socket, struct sockaddr_in server_address) {
    return bind(server_socket, (struct sockaddr *) &server_address, sizeof(server_address));
}

int listen_on_server_socket(int server_socket) {
    return listen(server_socket, SOMAXCONN);
}

void reap_child_process() {
    struct sigaction signal_action;
    signal_action.sa_handler = SIG_IGN;
    signal_action.sa_flags = 0;
    sigemptyset(&signal_action.sa_mask);
    sigaction(SIGCHLD, &signal_action, NULL);
}

int get_client_socket(int server_socket, struct sockaddr_in * client_address) {
    int client_address_length = sizeof(client_address);
    return accept(server_socket, (struct sockaddr *) client_address, &client_address_length);
}

int accept_client_connection_request(int server_socket) {
    int rv = -1, client_socket = -1;
    struct sockaddr_in client_address;
    while (1) {
        client_socket = get_client_socket(server_socket, &client_address);
        if (client_socket == -1) {
            exit_with_error("Error: Can't create client socket.");
        }
        rv = fork();
        if (rv == -1) {
			return rv;
        }
        if (rv == 0) {
			if (close_file_descriptor(server_socket) == -1) {
                send_internal_server_error(client_socket);
				exit_with_error("Error: Can't close server socket.");
			}
            break;
        }
        if (close_file_descriptor(client_socket) == -1) {
			exit_with_error("Error: Can't close client socket.");
		}
    }
    printf("Handling client: %s\n", inet_ntoa(client_address.sin_addr));
    return client_socket;
}


char * get_client_request(int client_socket) {
    char * receive_buffer = malloc(sizeof(char) * BUFFER_SIZE);
    if (read(client_socket, receive_buffer, BUFFER_SIZE) == -1) {
        send_internal_server_error(client_socket);
        exit_with_error("Error: Can't read request from client.");
    }
    return receive_buffer;
}

char ** tokenize_request(char * request) {
	char ** request_items_list = (char **) malloc(sizeof(char *) * (MAX_REQUEST_ITEMS + 1));
    for (int i = 0; i < MAX_REQUEST_ITEMS + 1; i++) {
        request_items_list[i] = (char *) malloc(sizeof(char) * REQUEST_ITEM_LENGTH);
        bzero(request_items_list[i], REQUEST_ITEM_LENGTH);
    }
    request_items_list[0] = strtok(request, " ");
    request_items_list[1] = strtok(NULL, " ");
    request_items_list[2] = strtok(NULL, "\r\n");
    request_items_list[3] = NULL;
    return request_items_list;
}

int is_valid_request_type(char * request_type) {
	return strcmp(request_type, "GET") == 0 ? 0 : -1;
}

void send_response(int client_socket, char * send_buffer, int count) {
    if (write(client_socket, send_buffer, count) == -1) {
        send_internal_server_error(client_socket);
        exit_with_error("Error: Can't send data to client.");
    }
}

char * get_header(int code) {
    char * header = NULL;
    switch (code) {
        case 200:
            header = "HTTP/1.1 200 Ok\r\n";
            break;
        case 405:
            header = "HTTP/1.1 405 Method Not Allowed\r\n";
            break;
        case 404:
            header = "HTTP/1.1 404 Not Found\r\n";
            break;
        case 415:
            header = "HTTP/1.1 415 Unsupported Media Type\r\n";
            break;
        case 500:
            header = "HTTP/1.1 500 Internal Server Error\r\n";
            break;
        default:
            header = "HTTP/1.1 500 Internal Server Error\r\n";
    }
    return header;
}

void send_header(int client_socket, int code, char * content_type) {
	char * header = get_header(code);
    char full_header[BUFFER_SIZE];
    strcpy(full_header, header);
    strcat(full_header, "Content-Type: ");
    strcat(full_header, content_type);
    strcat(full_header, "\r\n\r\n");
	send_response(client_socket, full_header, strlen(full_header));
}

int is_root_directory(char * path) {
    return strlen(path) == 0 ? 0 : -1;
}

int is_directory(char * path) {
    int rv = -1;
    struct stat buf;
    if (lstat(path, &buf) == -1) {
        if (errno == ENOENT) {
            rv = -1;
        } else {
            exit_with_error("Error: Can't decide if request is for file or directory.");   
        }
    }
    if (S_ISDIR(buf.st_mode)) {
        rv = 0;
    }
    return rv;
}

void get_total_entries_and_max_size_in_directory(char * path, int * total_entries, int * max_size) {
    DIR * dp = opendir(path);
    if (dp == NULL) {
        exit_with_error("Error: Can't open directory.");
    }
    errno = 0;
    struct dirent * entry = NULL;
    while ((entry = readdir(dp)) != NULL) {
        if (entry == NULL && errno != 0) {
            exit_with_error("Error: Can't read directory.");
        } else {
            if (entry->d_name[0] == '.') {
                continue;
            }
            if (strlen(entry->d_name) > *max_size) {
                *max_size = strlen(entry->d_name);
            }
            (*total_entries)++;
        }
    }
    closedir(dp);
}

int cmpstringp(const void *p1, const void *p2) {
    return strcmp(* (char * const *) p1, * (char * const *) p2);
}

void send_directory_listing(int client_socket, char * path){
    DIR * dp = opendir(path);
    if (dp == NULL) {
        send_internal_server_error(client_socket);
        exit_with_error("Error: Can't open directory.");
    }
    int rows = 0, cols = 0;
    get_total_entries_and_max_size_in_directory(path, &rows, &cols);
    char ** entries = (char **) malloc(sizeof(char *) * rows);
    for (int i = 0; i < rows; i++) {
        entries[i] = (char *) malloc(sizeof(char) * (cols + 128));
    }
    char * header = "<!DOCTYPE html><html><body><ul>";
    send_response(client_socket, header, strlen(header));
    errno = 0;
    char * name = NULL;
    struct dirent * entry = NULL;
    char directory_name[BUFFER_SIZE];
    int i = 0;
    while ((entry = readdir(dp)) != NULL) {
        if (entry == NULL && errno != 0) {
            send_internal_server_error(client_socket);
            exit_with_error("Error: Can't read directory.");
        } else {
            if (entry->d_name[0] == '.') {
                continue;
            }
            strcpy(directory_name, "<li>");
            strcat(directory_name, entry->d_name);
            strcat(directory_name, "</li>");
            strcpy(entries[i], directory_name);
            i++;
        }
    }
    qsort(entries, rows, sizeof(char *), cmpstringp);
    for (int i = 0; i < rows; i++) {
        send_response(client_socket, entries[i], strlen(entries[i]));
    }
    header = "</ul></body></html>";
    send_response(client_socket, header, strlen(header));
    closedir(dp);
}

int is_regular_file(char * path) {
    int rv = -1;
    struct stat buf;
    if (lstat(path, &buf) == -1) {
        if (errno == ENOENT) {
            rv = -1;
        } else {
            exit_with_error("Error: Can't decide if request is for file or directory.");   
        }
    }
    if (S_ISREG(buf.st_mode)) {
        rv = 0;
    }
    return rv;
}

char * is_requested_file_type_valid(char * file_name) {
    char * content_type = NULL;
    int file_name_length = strlen(file_name);
    for (int i = 0; extensions[i].extension != 0; i++) {
        int extension_length = strlen(extensions[i].extension);
        if (!strncmp(&file_name[file_name_length - extension_length], extensions[i].extension, extension_length)) {
            content_type = extensions[i].content_type;
            break;
        }
    }
    return content_type;
}

void send_file(int client_socket, char * path) {
    int fd = open(path, O_RDONLY);
	if (fd == -1) {
        send_internal_server_error(client_socket);
		exit_with_error("Error: Can't open file.");
	}
	char * send_buffer = malloc(sizeof(char) * BUFFER_SIZE);
	int rv = -1;
	while((rv = read(fd, send_buffer, BUFFER_SIZE))) {
		if (rv == -1) {
            send_internal_server_error(client_socket);
			exit_with_error("Error: Can't read file.");
		}
		send_response(client_socket, send_buffer, rv);
	}
	if (close_file_descriptor(fd) == -1) {
        send_internal_server_error(client_socket);
        exit_with_error("Error: Can't close file.");
    }
}

void send_internal_server_error(int client_socket) {
    char real_path[BUFFER_SIZE];
    char * path = "500.html";
    char * content_type = "text/html";
    snprintf(real_path, BUFFER_SIZE, "%s%s", RESPONSES_PATH, path);
    send_header(client_socket, INTERNAL_SERVER_ERROR, content_type);
    send_file(client_socket, real_path);
    exit(-1);
}

int close_file_descriptor(int file_descriptor) {
	return close(file_descriptor);
}

int shutdown_connection(int socket) {
    return shutdown(socket, SHUT_RDWR);
}
