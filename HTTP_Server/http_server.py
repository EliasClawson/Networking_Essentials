import logging
import random
import socket
import argparse
import os
import sys  # Import sys for exiting


def shuffle_text(text):
    text_line = text
    newline_count = text_line.count("\n")  # Count \n, replace at end of shuffled text
    text_line = text_line.replace("\n", "")
    char_list = list(text_line)
    random.shuffle(char_list)  # Actual shuffling here
    shuffled_string = "".join(char_list)
    shuffled_string += "\n" * newline_count
    return shuffled_string


def reverse_text(text):
    newline_count = text.count("\n")
    reversed_text = text.replace("\n", "")[::-1]  # Take out \n and reverse
    reversed_text += "\n" * newline_count  # Put \n back in
    return reversed_text


def randomize_text(text):
    def discard():
        return random.choices([True, False], weights=[1, 5])[0]

    def repeat(char):
        should_repeat = random.choices([True, False], weights=[1, 5])[0]

        if should_repeat:
            repeat_amount = int(random.paretovariate(1))
            return char * repeat_amount
        else:
            return char

    transformed_text = [repeat(c) for c in text if not discard()]

    if len(transformed_text) == 0:
        transformed_text = text[0]

    return "".join(transformed_text)


def parse_arguments():
    parser = argparse.ArgumentParser(description="TCP Server")

    # Adding arguments
    parser.add_argument("-p", "--port", type=int, default=8084, help="port to bind to")
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="turn on debugging output"
    )
    parser.add_argument(
        "-d", "--delay", action="store_true", help="add a delay for debugging purposes"
    )
    parser.add_argument(
        "-f", "--folder", type=str, default=".", help="folder from where to serve from"
    )

    return parser.parse_args()


def run(port, delay, folder):
    server_socket = socket.socket()
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind(("", port))
    server_socket.listen()

    try:
        while True:
            conn, address = server_socket.accept()
            logging.info(f"Connection from: {address}")

            while True:
                header = ""

                while True:
                    headerByte = conn.recv(1)
                    if not headerByte:
                        logging.error("Header error...")
                        return 1
                    header += headerByte.decode()
                    if header[-4:] == "\r\n\r\n":
                        logging.info("Received header, parsing...")
                        break

                code = "200 OK"

                firstSpace = header.index(" ")
                secondSpace = header.index(" ", firstSpace + 1)
                newLine = header.index("\r\n")
                endOfHeader = header.index("\r\n\r\n", newLine)
                method = header[0:firstSpace]
                logging.info(f"Method: '{method}'")
                path = "./" + folder + header[firstSpace + 1 : secondSpace]
                logging.info(f"Path: '{path}'")
                version = header[secondSpace + 1 : newLine]
                logging.info(f"Version: '{version}'")
                host = header[newLine + 2 : endOfHeader]
                logging.info(f"HostLine: '{host}'")

                if not version or not version or not host:
                    logging.error("400 Bad Request")
                    code = "400 Bad Request"

                if not os.path.isfile(path):
                    logging.error(f"File does not exist: '{path}'")
                    code = "404 Not Found"
                    path = folder + "/404.html"

                if not (method == "GET"):
                    code = "405 Method Not Allowed"

                logging.info("File exists!")
                fileSize = os.path.getsize(path)
                logging.info(f"File size: {fileSize}")

                if delay:
                    logging.info("Delaying response for testing...")
                    logging.info("End of delay")

                responseHeader = (
                    f"HTTP/1.1 {code}\r\n" f"Content-Length: {fileSize}\r\n" f"\r\n"
                ).encode()
                conn.sendall(responseHeader)

                chunk_size = 1000
                with open(path, "rb") as file:
                    while True:
                        chunk = file.read(chunk_size)
                        if not chunk:
                            logging.info("End of file")
                            break
                        logging.info("Here's a chunk...")
                        try:
                            conn.send(chunk)
                        except BrokenPipeError:
                            logging.error("Client disconnected prematurely.")

                    logging.info("Sent entire file")
                break

            conn.close()  # End connection after loop is finished

    except KeyboardInterrupt:
        logging.info("Server shutting down...")
        server_socket.close()  # Close the socket
        sys.exit(0)  # Exit gracefully with code 0


if __name__ == "__main__":
    args = parse_arguments()

    # Set the logging level based on the verbose flag
    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(
        level=log_level,  # Set the log level based on the -v flag
        format="*%(levelname)s* (Line %(lineno)d) - %(message)s",
    )

    # Run the server with the provided port
    run(args.port, args.delay, args.folder)
