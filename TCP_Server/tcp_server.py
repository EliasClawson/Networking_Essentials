import logging
import random
import socket
import argparse
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
    parser.add_argument("-p", "--port", type=int, default=8083, help="port to bind to")
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="turn on debugging output"
    )

    return parser.parse_args()


def run(port):
    server_socket = socket.socket()
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind(("", port))
    server_socket.listen()

    try:
        while True:
            conn, address = server_socket.accept()
            logging.info(f"Connection from: {address}")

            while True:
                action_data = conn.recv(2)
                if not action_data:
                    logging.info("Client disconnected...")
                    break
                action = action_data[0] >> 3
                logging.info(f"Action byte: {action}")

                message_top = (
                    int(action_data.hex()) & 0b00000111
                )  # Get the data from the first 2 bytes, mask off action
                message_length_data = conn.recv(2)  # Get the rest of the size data
                lowerinfo = int.from_bytes(message_length_data, byteorder="big")
                logging.info(f"lower end size: {lowerinfo}")
                length = (message_top << 16) + int.from_bytes(
                    message_length_data, byteorder="big"
                )  # Shift top to place and convert new data
                logging.info(f"Message is of length: {length}")

                sizeSoFar = 0
                while True:
                    logging.info(f"Receiving {length - sizeSoFar}")
                    message = conn.recv(
                        length - sizeSoFar
                    ).decode()  # Read 'message_length' bytes and decode it
                    # logging.info(f"Just got message: {message}")
                    logging.info(f"Just got size: {len(message)}")
                    logging.info(f"SizeSoFar: {sizeSoFar}")
                    logging.info(f"if({len(message)} + {sizeSoFar} == {length})")
                    if (
                        len(message) + sizeSoFar == length
                    ):  # If we received the full message, move on
                        break
                    else:  # If we haven't received the full message, loop and try to get the rest
                        sizeSoFar += len(message)
                        logging.info(f"Looping, sizeSoFar: {sizeSoFar}")

                # logging.info(f"Received message: '{message}'")

                transformed_message = ""
                if action == 0x01:  # UPPERCASE
                    transformed_message = message.upper().encode()
                elif action == 0x02:  # lowercase
                    transformed_message = message.lower().encode()
                elif action == 0x04:  # reverse
                    transformed_message = reverse_text(message).encode()
                elif action == 0x08:  # shuffle
                    transformed_message = shuffle_text(message).encode()
                elif action == 0x10:  # random
                    transformed_message = randomize_text(message).encode()
                else:
                    logging.info("Invalid action")
                    transformed_message = "error".encode()

                message_length = len(
                    transformed_message
                )  # Length of the transformed message
                logging.info(f"Message length sending: {message_length}")
                message_length_bytes = message_length.to_bytes(
                    4, byteorder="big"
                )  # convert to proper format
                response = (
                    message_length_bytes + transformed_message
                )  # Attach size to message

                logging.info(f"Response: {response}")
                conn.send(response)

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
    run(args.port)
