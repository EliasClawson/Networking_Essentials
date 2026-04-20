#include "log.h"

#include "tcp_client.h"

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/*
Description: Prints the usage commands of the program
Return value: void
*/
void print_usage() {
    printf("Usage: tcp_client [--help] [-v] [-h HOST] [-p PORT] ACTION MESSAGE\n\n");
    printf("  ACTION   Must be uppercase, lowercase, reverse,\n");
    printf("           shuffle, or random.\n\n");
    printf("  MESSAGE  Message to send to the server\n\n");
    printf("  Options:\n");
    printf("    --help\n");
    printf("    -v, --verbose\n");
    printf("    --host HOSTNAME, -h HOSTNAME\n");
    printf("    --port PORT, -p PORT\n");
}

/*
Description:
    Retrieves IP address from given socket address
Arguments:
    struct sockaddr: Socket address
*/
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

/*
Description:
    Parses the commandline arguments and options given to the program.
Arguments:
    int argc: the amount of arguments provided to the program (provided by the main function)
    char *argv[]: the array of arguments provided to the program (provided by the main function)
    Config *config: An empty Config struct that will be filled in by this function.
Return value:
    Returns a 1 on failure, 0 on success
*/
int tcp_client_parse_arguments(int argc, char *argv[], Config *config) {
    int opt;
    char *host = "localhost"; // Default to localhost:8080
    char *port = "8080";
    char *action = NULL;
    char *message = NULL;

    // Initialize message to empty string
    message = malloc(1);
    message[0] = '\0';
    static struct option longopts[] = {{"help", no_argument, NULL, 'x'},
                                       {"verbose", no_argument, NULL, 'v'},
                                       {"host", required_argument, NULL, 'h'},
                                       {"port", required_argument, NULL, 'p'},
                                       {0, 0, 0, 0}};
    // Process arguments using getopt
    while ((opt = getopt_long(argc, argv, "xh:p:v", longopts, NULL)) != -1) {
        switch (opt) {
        case 'x':
            print_usage();
            return 1;
            break;
        case 'h':
            host = optarg;
            break;
        case 'p':
            port = optarg;
            break;
        case 'v':
            log_set_level(LOG_INFO); // Allows all log_info() commands to output
            log_info("***Verbose mode enabled***");
            break;
        default:
            log_error("Invalid option: -%c", opt);
            print_usage();
            free(message);
            return 1;
        }
    }

    // Check for required arguments (action and message)
    if (optind >= argc) {
        log_error("Missing required arguments: ACTION MESSAGE");
        print_usage();
        free(message);
        return 1;
    }

    // Validate and assign action
    action = argv[optind];
    if (strcmp(action, "uppercase") && strcmp(action, "lowercase") && strcmp(action, "reverse") &&
        strcmp(action, "shuffle") && strcmp(action, "random")) {
        log_error("Invalid action: %s", action);
        print_usage();
        free(message);
        return 1;
    }
    ////////////////////////////////////////////////////////////////////////////////////////
    // Handle remaining arguments as message
    int message_length = 0;
    for (int i = optind + 1; i < argc; i++) {
        message_length += strlen(argv[i]) + 1; // +1 for space between words
    }

    // Reallocate memory for message with enough space
    message = realloc(message, message_length);
    if (message == NULL) {
        log_error("Failed to allocate memory for message");
        return 1;
    }
    // Build message string
    message[0] = '\0'; // Reset message to empty
    strcat(message, argv[optind + 1]);
    if (argv[optind + 2] != NULL) {
        log_error("Too many arguments");
        return 1;
    }
    /////////////////////////////////////////////////////////////////////////////////////////
    // Compile message with Action and Length
    int len;
    len = strlen(action) + 1 + strlen(message) + 1 + strlen(message);
    config->message = malloc(len + 1); // +1 for null terminator
    sprintf(config->message, "%s %ld %s", action, strlen(message), message);
    log_info("Host: %s, Port: %s", host, port);
    log_info("Message: %s", config->message);

    // Assign values to config struct
    config->host = host;
    config->port = port;
    config->action = action;

    return 0;
}

///////////////////////////////////////////////////////////////////////
/////////////////////// SOCKET RELATED FUNCTIONS //////////////////////
///////////////////////////////////////////////////////////////////////

/*
Description:
    Creates a TCP socket and connects it to the specified host and port.
Arguments:
    Config config: A config struct with the necessary information.
Return value:
    Returns the socket file descriptor or -1 if an error occurs.
*/
int tcp_client_connect(Config config) {
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(config.host, config.port, &hints, &servinfo)) != 0) {
        log_error("Failed to connect to Host: %s", config.host);
        return -1;
    }

    // loop through all the results and connect to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            log_error("Client error: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            log_error("client error: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        log_error("Client error: failed to connect\n");
        return -1;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    log_info("client: connecting to %s", s);

    freeaddrinfo(servinfo);
    log_info("Connected to Socket: %d", sockfd);
    return sockfd;
}

/*
Description:
    Creates and sends request to server using the socket and configuration.
Arguments:
    int sockfd: Socket file descriptor
    Config config: A config struct with the necessary information.
Return value:
    Returns a 1 on failure, 0 on success
*/
int tcp_client_send_request(int sockfd, Config config) {
    char *msg = config.message;
    int len, bytes_sent;

    len = strlen(msg);
    bytes_sent = send(sockfd, msg, len, 0);
    log_info("Sent message to %s", config.host);
    log_info("Bytes sent: %d", bytes_sent);
    return 0;
}

/*
Description:
    Receives the response from the server. The caller must provide an already allocated buffer.
Arguments:
    int sockfd: Socket file descriptor
    char *buf: An already allocated buffer to receive the response in
    int buf_size: The size of the allocated buffer
Return value:
    Returns a 1 on failure, 0 on success
*/
int tcp_client_receive_response(int sockfd, char *buf, int buf_size) {
    int numbytes;
    if ((numbytes = recv(sockfd, buf, buf_size - 1, 0)) == -1) {
        if (numbytes == 0) { // Connection closed by server
            log_error("Server closed the connection");
        } else {
            log_error("Failed to receive response");
        }
        return 1; // Indicate failure
    }
    log_info("Receiving Data...");

    buf[numbytes] = '\0';

    log_info("Received %d bytes from server: %s", numbytes, buf);

    return 0; // Indicate success
}

/*
Description:
    Closes the given socket.
Arguments:
    int sockfd: Socket file descriptor
Return value:
    Returns a 1 on failure, 0 on success
*/
int tcp_client_close(int sockfd) {
    close(sockfd);
    return 0;
}