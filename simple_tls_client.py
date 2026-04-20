import crypto_utils as utils
from message import Message, MessageType
import argparse
import logging
import sys
import socket
import os


def parse_arguments():
    parser = argparse.ArgumentParser(description="Simple TLS Client")
    parser.add_argument(
        "file",
        type=str,
        help="The file name to save to. It must be a PNG file extension. Use - for stdout.",
    )
    parser.add_argument(
        "-p",
        "--port",
        type=int,
        default=8087,
        help="Port to connect to.",
    )
    parser.add_argument(
        "--host", type=str, default="localhost", help="Hostname to connect to."
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Turn on debugging output."
    )

    return parser.parse_args()


def connect(host, port):
    # Establish connection
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        logging.info("Socket created successfully.")

        # Connect to the server
        s.connect((host, port))
        logging.info(f"Connected to {host}:{port}.")
        return s
    except Exception as e:
        logging.error(f"Failed to establish connection: {e}")
        return -1


def send_client_hello(socket):
    try:
        hello_message = Message(MessageType.HELLO)
        socket.sendall(hello_message.to_bytes())

        logging.info("Sent 'HELLO' message to server")
        return hello_message
    except Exception as e:
        logging.error(f"Failed to send 'HELLO' message: {e}")
        return -1


def receive_server_certificate(socket):
    mess = Message.from_socket(socket)
    if not mess or mess.type != MessageType.CERTIFICATE:
        logging.error("We didn't get the certificate. Something is wrong...")
        logging.error(f"Message type received: {mess.type}.")
        return -1
    server_nonce_raw = mess.data[:32]
    certificate_raw = mess.data[32:]
    certificate = utils.load_certificate(certificate_raw)

    if not certificate:
        logging.error("Server certificate verification failed.")
        return -1

    # Extract nonce and public key from the verified certificate
    server_nonce = server_nonce_raw  # utils.extract_nonce(certificate)

    return server_nonce, certificate.public_key(), mess


def receive_and_process_data(socket, server_enc_key, server_int_key, output_file):
    sequence_number = 0
    data_chunks = []

    while True:
        # Receive the data message from the server
        encrypted_message = Message.from_socket(socket)
        if not encrypted_message:
            break

        if encrypted_message.type != MessageType.DATA:
            logging.error(
                f"Unexpected message type: {encrypted_message.type}. Expected DATA."
            )
            return -1
        # Decrypt the message
        decrypted_payload = utils.decrypt(encrypted_message.data, server_enc_key)
        # Extract sequence number, data chunk, and MAC
        received_seq_num = int.from_bytes(decrypted_payload[:4], byteorder="big")
        data_chunk = decrypted_payload[4:-32]
        received_mac = decrypted_payload[-32:]
        # Verify sequence number
        if received_seq_num != sequence_number:
            logging.error(
                f"Sequence number mismatch. Expected {sequence_number}, got {received_seq_num}."
            )
            return -1
        # Compute MAC over the sequence number and data chunk
        computed_mac = utils.mac(data_chunk, server_int_key)
        # Verify MAC
        if computed_mac != received_mac:
            logging.error("MAC verification failed. Data integrity compromised.")
            return -1
        # Append the valid data chunk
        data_chunks.append(data_chunk)
        sequence_number += 1

    # Write the collected data to the specified output
    complete_data = b"".join(data_chunks)
    if output_file == "-":
        sys.stdout.buffer.write(complete_data)
        sys.stdout.flush()
        logging.info("Data written to stdout.")
    else:
        try:
            with open(output_file, "wb") as f:
                f.write(complete_data)
            logging.info(f"Data written to file: {output_file}")
        except Exception as e:
            logging.error(f"Failed to write data to file {output_file}: {e}")
    return 0


def main():
    args = parse_arguments()
    host = args.host
    port = args.port
    file = args.file

    # Set the logging level based on the verbose flag
    log_level = logging.INFO if args.verbose else logging.ERROR
    logging.basicConfig(
        level=log_level,  # Set the log level based on the -v flag
        format="*%(levelname)s* (Line %(lineno)d) - %(message)s",
    )

    logging.info("Starting Simple TLS Client")

    # Connect to server
    s = connect(host, port)
    if s == -1:
        sys.exit(1)

    # Perform handshake
    handshake_message = send_client_hello(s)

    # Receive certificate and nonce
    server_nonce, public_key, certificate_message = receive_server_certificate(s)

    # Generate Nonce and Keys
    client_nonce_raw = os.urandom(32)  # Generate client nonce
    client_nonce = utils.encrypt_with_public_key(client_nonce_raw, public_key)
    client_nonce_mess = Message(MessageType.NONCE, client_nonce)
    s.sendall(client_nonce_mess.to_bytes())

    keys = utils.generate_keys(client_nonce_raw, server_nonce)
    server_enc_key, server_int_key, client_enc_key, client_int_key = keys

    server_mac_gen = utils.mac(
        handshake_message.to_bytes()
        + certificate_message.to_bytes()
        + client_nonce_mess.to_bytes(),
        server_int_key,
    )
    server_hash_message = Message.from_socket(s)

    received_server_mac = server_hash_message.data
    if server_mac_gen != received_server_mac:
        logging.error("Server hash verification failed")
        logging.info(f"Generated Mac: {server_mac_gen}")
        logging.info(f"Received Mac: {received_server_mac}")
        return -1
    else:
        logging.info("Server hash verified")

    client_mac_gen = utils.mac(
        handshake_message.to_bytes()
        + certificate_message.to_bytes()
        + client_nonce_mess.to_bytes(),
        client_int_key,
    )
    client_mac_message = Message(MessageType.HASH, client_mac_gen)

    s.sendall(client_mac_message.to_bytes())

    status = receive_and_process_data(s, server_enc_key, server_int_key, file)
    sys.exit(status)


if __name__ == "__main__":
    main()
