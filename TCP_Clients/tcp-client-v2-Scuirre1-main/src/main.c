#include "log.h"
#include "tcp_client.h"
#include <stdio.h>

#define LOG_NONE 0
#define LOG_DEBUG 1
#define LOG_INFO 2
#define LOG_WARN 3
#define LOG_ERROR 4

#define MAX_MESSAGE_SIZE 1024

int messageCounter;
/*
Description: TODO

returns 0 if finished, 1(or more) if not finished receiving
*/
int handle_response(char *response) {
    log_info("Handling response!");
    write(STDOUT_FILENO, response, strlen(response));
    messageCounter--;
    return messageCounter;
}

int main(int argc, char *argv[]) {
    FILE *file;
    Config config;            // Struct to hold information for connection and request
    log_set_level(LOG_ERROR); // Only report errrors, unless specified with --verbose

    if (tcp_client_parse_arguments(argc, argv, &config) != EXIT_SUCCESS) {
        return EXIT_FAILURE; // End early with error if there's a problem
    }
    log_info("Arguments parsed, opening file");
    file = tcp_client_open_file(config.file);
    if (file == NULL) {
        return EXIT_FAILURE;
    }
    int sockfd = tcp_client_connect(config);
    if (sockfd == -1) {
        return EXIT_FAILURE; // End early with error if there's a problem
    }

    messageCounter = 0;
    log_info("Reading requests...");
    while (1) { // Infinite loop until a break statement is encountered
        char *action = "";
        char *message = "";
        int bytes_read = tcp_client_get_line(file, &action, &message);
        if (bytes_read == -1) {
            // Handle end of file or error
            // free(message);
            // free(action);
            log_info("No line here");
            break;
        }

        // Process the action and message
        tcp_client_send_request(sockfd, action, message);
        messageCounter++;
        // Free allocated memory
        free(action);
        free(message);
    }

    log_info("******All requests sent!******");
    if (messageCounter == 0) {
        log_error("Nothing sent to server.");
        return EXIT_FAILURE;
    }
    log_info("******Receiving data******");
    int result = tcp_client_receive_response(sockfd, handle_response);
    if (result != EXIT_SUCCESS) {
        log_error("Failed to receive response from server");
        return EXIT_FAILURE; // End early with error if there's a problem
    }
    if (tcp_client_close(sockfd) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
