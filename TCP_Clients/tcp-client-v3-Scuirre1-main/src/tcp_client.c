#include "tcp_client.h"
#include "log.h"
#include <arpa/inet.h> // For ntohl()
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
    printf("Usage: tcp_client [--help] [-v] [-h HOST] [-p PORT] FILE\n\n");
    printf("  FILE   A file name containing actions and messages to\n");
    printf("         send to the server. If \" - \" is provided, stdin will\n");
    printf("         be read.\n\n");
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
    char *host = TCP_CLIENT_DEFAULT_HOST; // Default to localhost:8080
    char *port = TCP_CLIENT_DEFAULT_PORT;
    char *file = NULL;

    static struct option longopts[] = {
        {"help", no_argument, NULL, 'x'}, // x cause I wasn't sure what else to do
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
            return 1;
        }
    }

    // Check for filename
    if (optind >= argc) {
        log_error("Missing required arguments: FILE");
        print_usage();
        return 1;
    }
    if (argv[optind + 1] != NULL) {
        log_error("Unexpected Argument: %s", argv[optind + 1]);
        return 1;
    }

    // Assign the file name to the config
    file = argv[optind];

    log_info("Host: %s, Port: %s, File: %s", host, port, file);

    // Assign values to config struct
    config->host = host;
    config->port = port;
    config->file = file;
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
    char *action: The action that will be sent
    char *message: The message that will be sent
Return value:
    Returns a 1 on failure, 0 on success
*/
int tcp_client_send_request(int sockfd, char *action, char *message) {
    char *msg;
    uint32_t packed_header;
    int bytes_sent, total_sent = 0;
    int message_len = strlen(message);
    log_info("Packaging message of length: %d", message_len);
    // Map the action to the corresponding binary value
    uint32_t action_code;
    if (strcmp(action, "uppercase") == 0)
        action_code = 0x01;
    else if (strcmp(action, "lowercase") == 0)
        action_code = 0x02;
    else if (strcmp(action, "reverse") == 0)
        action_code = 0x04;
    else if (strcmp(action, "shuffle") == 0)
        action_code = 0x08;
    else if (strcmp(action, "random") == 0)
        action_code = 0x10;
    else {
        log_error("Invalid action");
        return 1; // Invalid action
    }
    log_info("Action code: %d", action_code);

    if (message_len > (1 << 27) - 1) {
        log_error("Message too long");
        return 1; // Message length exceeds 27-bit limit
    }

    // Pack action and message length into 32-bit header
    packed_header = (action_code << 27) | (message_len & 0x07FFFFFF);

    // Allocate memory for the full message (header + data)
    int len = 4 + message_len; // 4 bytes for header, rest for message
    msg = malloc(len);
    if (!msg) {
        log_error("Memory allocation failed");
        return 1;
    }

    // Copy the packed header and the message
    *(uint32_t *)msg = htonl(packed_header); // Convert header to big-endian
    memcpy(msg + 4, message, message_len);   // Copy the message data after header

    log_info("Send request!");
    // Send the full message, looping if necessary
    while (total_sent < len) {
        bytes_sent = send(sockfd, msg + total_sent, len - total_sent, 0);
        if (bytes_sent == -1) {
            log_error("Send failed.");
            free(msg);
            return 1; // Error
        }
        total_sent += bytes_sent;
    }

    log_info("Sent message to host with action: %s", action);
    free(msg);
    return 0;
}

/*TODO WRITE DESCRIPTION*/
int doubleBuf(char **buf, size_t *bufSize) {
    log_info("Increasing buffer size from: %d, to: %d", *bufSize, *bufSize * 2);
    *bufSize *= 2;
    char *newBuf = realloc(*buf, *bufSize);
    if (newBuf == NULL) {
        log_error("Failed to resize output buffer");
        free(*buf);
        return 1;
    }
    *buf = newBuf;
    return 0;
}

/*
Description:
    Receives the response from the server. The caller must provide a function pointer that handles
the response and returns a true value if all responses have been handled, otherwise it returns a
    false value. After the response is handled by the handle_response function pointer, the response
    data can be safely deleted. The string passed to the function pointer must be null terminated.
Arguments:
    int sockfd: Socket file descriptor
    int (*handle_response)(char *): A callback function that handles a response
Return value:
    Returns a 1 on failure, 0 on success
*/
int tcp_client_receive_response(int sockfd, int (*handle_response)(char *)) {
    char *inputBuf = malloc(4); // Buffer to hold 4-byte message length
    char *outputBuf = NULL;     // Will hold the response
    int numbytes = 0;
    uint32_t messageSize; // 32-bit message length
    size_t sizeSoFar = 0;
    int messagesLeft = 10;

    while (messagesLeft > 0) { // Main loop, only exits when all messages are received
        sizeSoFar = 0;

        log_info("Receiving Message size");
        numbytes = recv(sockfd, inputBuf, 4, 0); // Expecting exactly 4 bytes for the size
        if (numbytes != 4) {
            log_error("Failed to receive message size.");
            free(inputBuf);
            return 1;
        }

        messageSize = ntohl(*(uint32_t *)inputBuf);
        log_info("Receiving message of size: %d", messageSize);

        log_info("Creating output buffer");
        outputBuf = realloc(outputBuf, messageSize + 1); // +1 for null terminator
        if (outputBuf == NULL) {
            log_error("Memory allocation failed.");
            free(inputBuf);
            return 1;
        }

        log_info("Receiving message...");
        sizeSoFar = 0;
        while (sizeSoFar < messageSize) {
            numbytes = recv(sockfd, outputBuf + sizeSoFar, messageSize - sizeSoFar, 0);
            log_info("Size Received so far: %d", numbytes + sizeSoFar);
            if (numbytes <= 0) {
                log_error("Failed to receive message content.");
                free(inputBuf);
                free(outputBuf);
                return 1;
            }
            sizeSoFar += numbytes;
        }
        outputBuf[sizeSoFar] = '\0'; // Null-terminate the response

        log_info("Passing buffer to handle_response");
        messagesLeft = handle_response(outputBuf);

        if (messagesLeft == 0) {
            break; // All responses handled
        }
    }

    // Cleanup
    free(inputBuf);
    free(outputBuf);

    return 0;
}

/*
Description:
    Closes the given socket.
Arguments:
    int sockfd: Socket file descriptor
Return value:
    Returns a 1 on failure, 0 on success
*/
int tcp_client_close(int sockfd) { return close(sockfd); }

///////////////////////////////////////////////////////////////////////
//////////////////////// FILE RELATED FUNCTIONS ///////////////////////
///////////////////////////////////////////////////////////////////////

/*
Description:
    Opens a file.
Arguments:
    char *file_name: The name of the file to open
Return value:
    Returns NULL on failure, a FILE pointer on success
*/
FILE *tcp_client_open_file(char *file_name) {
    log_info("FileName: %s", file_name);
    FILE *file;
    if (strcmp(file_name, "-") == 0) {
        file = stdin;
    } else {
        file = fopen(file_name, "r");
        if (file == NULL) {
            // log_error("Failed to open: %s", file_name);
            // printf("Apparently now I need to print something to stdout here? Not sure why "
            //"requirements keep changing...");
            perror("Error opening file");
            return NULL;
        }
    }

    return file;
}

/*
Description:
    Gets the next line of a file, filling in action and message. This function should be similar
    design to getline() (https://linux.die.net/man/3/getline). *action and message must be
allocated by the function and freed by the caller.* When this function is called, action must
point to the action string and the message must point to the message string. Arguments: FILE
*fd: The file pointer to read from char **action: A pointer to the action that was read in char
**message: A pointer to the message that was read in Return value: Returns -1 on failure, the
number of characters read on success
*/
int tcp_client_get_line(FILE *fd, char **action, char **message) {
    log_info("Getting new line...");
    size_t line_length = 0;
    ssize_t bytes_read;
    char *line_buffer = NULL;
    while ((bytes_read = getline(&line_buffer, &line_length, fd)) != -1) {
        log_info("Bytes_read: %d", bytes_read);
        // Check if the line is empty or consists only of newline character
        if (bytes_read <= 1 || line_buffer[0] == '\n') {
            free(line_buffer);
            line_buffer = NULL;
            continue; // Skip empty or invalid lines
        }
        log_info("Line not blank: %s", line_buffer);
        // Extract action and message (assuming space separates them)
        char *delim = strchr(line_buffer, ' ');

        if (delim == NULL || (delim == line_buffer)) {
            log_info("This is an invalid line: No space");
            // Invalid line format (no space)
            free(line_buffer);
            line_buffer = NULL;
            continue; // Skip invalid lines
        }
        log_info("Message: %s", (delim + 1));
        if ((delim + 1) == NULL || *(delim + 1) == '\n') {
            log_info("This is an invalid line: No message");
            // Invalid line format (no space)
            free(line_buffer);
            line_buffer = NULL;
            continue; // Skip invalid lines
        }
        log_info("Potentially valid line...");
        // Extract action
        *action = malloc(delim - line_buffer + 1); // +1 for null terminator
        // Add newline characters to the beginning

        strncpy(*action, line_buffer, delim - line_buffer);
        (*action)[delim - line_buffer] = '\0'; // Null terminate
        // Extract message (including trailing space)
        *message = malloc(strlen(delim + 1) + 1); // +1 for null terminator
        strcpy(*message, delim + 1);

        if (strcmp(*action, "uppercase") && strcmp(*action, "lowercase") &&
            strcmp(*action, "reverse") && strcmp(*action, "shuffle") && strcmp(*action, "random")) {
            free(*action);
            free(*message);

            log_info("Skipping invalid action: %s", *action);
            continue;
        }
        log_info("Valid action.");
        // Free the original buffer (no longer needed)
        free(line_buffer);

        // Return the number of characters read (excluding null terminator)
        return bytes_read;
    }

    // Handle end of file
    free(line_buffer);
    return -1; // Error reading from file
}

/*
Description:
    Closes a file.
Arguments:
    FILE *fd: The file pointer to close
Return value:
    Returns a 1 on failure, 0 on success
*/
int tcp_client_close_file(FILE *fd) { return fclose(fd); }