#include "log.h"
#include "tcp_client.h"
#include <stdio.h>

#define LOG_NONE 0
#define LOG_DEBUG 1
#define LOG_INFO 2
#define LOG_WARN 3
#define LOG_ERROR 4

#define MAX_MESSAGE_SIZE 1024

int main(int argc, char *argv[]) {
    Config config;            // Struct to hold information for connection and request
    log_set_level(LOG_ERROR); // Only report errrors, unless specified with --verbose

    if (tcp_client_parse_arguments(argc, argv, &config) == 1) {
        return 1; // End early with error if there's a problem
    }

    int sockfd = tcp_client_connect(config);
    if (sockfd == -1)
        return 1; // End early with error if there's a problem

    tcp_client_send_request(sockfd, config);

    char buf[MAX_MESSAGE_SIZE];
    if (buf == NULL) {
        log_error("Failed to allocate memory for buffer");
        return 1; // End early with error if there's a problem
    }

    int result = tcp_client_receive_response(sockfd, buf, MAX_MESSAGE_SIZE);
    if (result != 0) {
        log_error("Failed to receive response from server");
        return 1; // End early with error if there's a problem
    }

    tcp_client_close(sockfd);

    printf("%s\n", buf); // Print response

    return 0;
}
