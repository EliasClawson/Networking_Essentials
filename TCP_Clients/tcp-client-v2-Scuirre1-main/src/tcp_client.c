#include "tcp_client.h"
#include "log.h"
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
    Config config: A config struct with the necessary information.
Return value:
    Returns a 1 on failure, 0 on success
*/
int tcp_client_send_request(int sockfd, char *action, char *message) {
    char *msg;
    int len, bytes_sent;

    len = strlen(action) + 1 + strlen(message) + 1 + snprintf(NULL, 0, "%ld", strlen(message));
    msg = malloc(len + 1);

    sprintf(msg, "%s %ld %s", action, strlen(message), message);
    bytes_sent = send(sockfd, msg, strlen(msg), 0);
    // Check for errors TODO
    log_info("Sent message to host: %s", msg);
    log_info("Bytes sent: %d", bytes_sent);
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
    // Full receive loop:
    //  -- Runs recv, stores in input buf
    //  -- Check if last message
    //  -- Transfer input to output
    //  -- Handle output

    // Types of messages to handle:
    // -- Full response in message
    // -- Response starts in message, too big
    // -- Message split between responses
    // -- Response ends in message
    // -- Last message handled a little differently

    char *inputBuf; // Holds data from recv()
    size_t inputBufSize = 12;
    char *outputBuf; // Holds data for output
    size_t outputBufSize = 100;
    outputBuf = malloc(outputBufSize);
    int numbytes = 0;
    int remainder = 0;
    size_t sizeSoFar = 0;
    int messagesLeft = 10;

    while (1) { // Main loop, only exits when all messages received
        inputBuf = malloc(inputBufSize + 1);
        log_info("Created inputBuf, recving....");
        numbytes = recv(sockfd, inputBuf, inputBufSize, 0);
        log_info("numbytes: %d", numbytes);
        if (numbytes == -1) {
            // Handle other errors
            perror("recv");
            return 1;
        }
        inputBuf[inputBufSize] = '\0';
        log_info("----TOP OF LOOP------");
        log_info("inputBuf: '%s'", inputBuf);
        log_info("outputBuf: '%s'", outputBuf);
        log_info("Remainder: %d", remainder);
        log_info("Messages left: %d", messagesLeft);
        log_info("sizeSoFar: %d", sizeSoFar);
        log_info("--Entering conditionals--");
        if (remainder <= (int)inputBufSize && messagesLeft != 1) {
            log_info("---In messages with start numbers---");
            log_info("remainder: %d", remainder);
            char *spaceSpot = strchr(inputBuf + remainder, ' ');
            log_info("Space Location: %d", spaceSpot - inputBuf);
            size_t usableData = (inputBufSize - (spaceSpot - inputBuf + 1));
            log_info("Useable data: %d", usableData);
            log_info("inputBuf: %s", inputBuf);
            log_info("OutputBuf: %s", outputBuf);
            size_t sizeOfMessage = strtol(inputBuf + remainder, &spaceSpot, 10);
            log_info("Size of message: %d", sizeOfMessage);
            if ((sizeOfMessage > usableData) && (remainder == 0)) {
                // If message is too big, but this is not the second part
                log_info("Grabbing begining of message (when too big for single input)");
                log_info("Size so Far: %d", sizeSoFar);
                while (usableData >= outputBufSize) {
                    doubleBuf(&outputBuf, &outputBufSize);
                }
                memcpy(outputBuf, spaceSpot + 1, usableData); // Copy entire usable data in
                log_info("outputBuf: %s", outputBuf);
                sizeSoFar += usableData;
                remainder = sizeOfMessage - usableData;
                free(inputBuf);
                continue;
            } else if (sizeOfMessage > usableData) { // Buf is split between messages
                log_info("Reached split!");
                log_info("sizeSoFar: %d, remainder: %d, outputBuf: %s", sizeSoFar, remainder,
                         outputBuf);
                while ((sizeSoFar + remainder) >= outputBufSize) {
                    log_info("Doubling outputBuf");
                    doubleBuf(&outputBuf, &outputBufSize);
                }
                memcpy(outputBuf + sizeSoFar, inputBuf, remainder);
                outputBuf[sizeSoFar + remainder] = '\0';
                log_info("Copied last part into outputBuf: %s", outputBuf);
                sizeSoFar = 0;
                messagesLeft = handle_response(outputBuf);
                if (messagesLeft == 0) { // Break after all messages received.
                    free(outputBuf);
                    outputBuf = malloc(outputBufSize);
                    break;
                }
                sizeSoFar = 0;
                remainder = 0;
                log_info("Remainder: %d", remainder);
                log_info("Usable data: %d", usableData);
                free(outputBuf);
                outputBuf = malloc(outputBufSize);
                while ((usableData) >= outputBufSize) {
                    log_info("Doubling outputBuf");
                    doubleBuf(&outputBuf, &outputBufSize);
                }
                memcpy(outputBuf, spaceSpot + 1, usableData);
                remainder = sizeOfMessage - usableData;
                sizeSoFar = usableData;
                log_info("Remainder now: %d", remainder);
                log_info("At this point, outputBuf: %s", outputBuf);
                free(inputBuf);
                continue;
            } else { // Entire message is in input buf, or last one
                log_info("End of message here");
                log_info("outputBuf to finish: %s", outputBuf);
                log_info("Remainder: %d, inputBut: %s", remainder, inputBuf);

                if (remainder == 0) {
                    remainder = sizeOfMessage;
                }
                if (remainder == numbytes) {
                    log_info("Just contains end of message");
                    while ((sizeSoFar + remainder) >= outputBufSize) {
                        log_info("Doubling outputBuf");
                        doubleBuf(&outputBuf, &outputBufSize);
                    }
                    memcpy(outputBuf + sizeSoFar, inputBuf, remainder);
                    outputBuf[sizeSoFar + remainder] = '\0';
                } else {
                    log_info("contains next message...");
                    log_info("Useable data: %d", usableData);
                    while ((remainder) >= (int)outputBufSize) {
                        log_info("Doubling outputBuf");
                        doubleBuf(&outputBuf, &outputBufSize);
                    }
                    log_info("Copying input to output at %d", sizeSoFar);
                    log_info("Space spot here is: %d", spaceSpot - inputBuf);
                    if ((spaceSpot - inputBuf) < (int)inputBufSize) {
                        memcpy(outputBuf + sizeSoFar, spaceSpot, remainder);
                    } else {
                        memcpy(outputBuf + sizeSoFar, inputBuf, remainder);
                    }
                }
                log_info("After finishing, output: %s", outputBuf);
                outputBuf[sizeSoFar + remainder] = '\0';
                free(inputBuf);
                remainder = 0;
                sizeSoFar = 0;
                messagesLeft = handle_response(outputBuf);
                if (messagesLeft == 0) { // break if no messages left
                    free(outputBuf);
                    break;
                }
                continue;
            }
        } else {
            log_info("No end of message contained (Or last one...)");
            log_info("Here, input: %s\nHere, output: %s", inputBuf, outputBuf);
            log_info("SizeSoFar: %d, remainder: %d, numbytes: %d", sizeSoFar, remainder, numbytes);
            if (sizeSoFar == 0 && remainder == 0) {
                log_info("Start of last message");
                char *spaceSpot = strchr(inputBuf + remainder, ' ');
                log_info("Space Location: %d", spaceSpot - inputBuf);
                size_t usableData = (inputBufSize - (spaceSpot - inputBuf + 1));
                log_info("Useable data: %d", usableData);
                log_info("inputBuf: %s", inputBuf);
                log_info("OutputBuf: %s", outputBuf);
                size_t sizeOfMessage = strtol(inputBuf + remainder, &spaceSpot, 10);

                log_info("Size of message: %d", sizeOfMessage);
                remainder = sizeOfMessage;
                sizeSoFar = 0;
                log_info("ERRRORHEREEE___ usabledata: %d", usableData);
                while ((usableData) >= outputBufSize) {
                    log_info("Doubling outputBuf");
                    doubleBuf(&outputBuf, &outputBufSize);
                }
                log_info("After freeze?");
                memcpy(outputBuf, spaceSpot + 1, usableData);
                remainder -= usableData;
                sizeSoFar = usableData;
                log_info("Just before free..");
                free(inputBuf);
                log_info("After free...");
                continue;
            }
            log_info("Remainder: %d", remainder);
            log_info("SizeSoFar: %d", sizeSoFar);
            log_info("outputBuf: %s", outputBuf);
            log_info("inputBuf: %s", inputBuf);
            if (remainder < (int)inputBufSize) {
                log_info("Finishing message");
                while ((sizeSoFar + remainder) >= outputBufSize) {
                    log_info("Doubling outputBuf");
                    doubleBuf(&outputBuf, &outputBufSize);
                }
                if (remainder > 0) {
                    memcpy(outputBuf + sizeSoFar, inputBuf, remainder);
                    outputBuf[sizeSoFar + remainder] = '\0';
                } else {
                    memcpy(outputBuf + sizeSoFar, inputBuf, inputBufSize);
                    outputBuf[sizeSoFar + inputBufSize] = '\0';
                }
            } else {
                log_info("Filling and continueing");
                while ((sizeSoFar + inputBufSize) >= outputBufSize) {
                    log_info("Doubling outputBuf");
                    doubleBuf(&outputBuf, &outputBufSize);
                }
                memcpy(outputBuf + sizeSoFar, inputBuf, inputBufSize);
            }
            log_info("outputBuf fills to: %s", outputBuf);
            sizeSoFar += inputBufSize;
            remainder -= inputBufSize;
            free(inputBuf);
            log_info("After free, remainder: %d", remainder);
            if (remainder > 0) {
                log_info("continueing...");
                continue;
            } else {
                log_info("Exiting and out");
                handle_response(outputBuf);
                free(outputBuf);
                break;
            }
        }
        log_error("-------------Reaching end of whileloop....---------------");
    }

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
    FILE *file;
    if (strcmp(file_name, "-") == 0) {
        file = stdin;
    } else {
        file = fopen(file_name, "r");
        if (file == NULL) {
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