import logging  # For logging errors
import socket  # For connecting to client
import argparse  # For interpretting starting input
import os  # For opening and reading files
import sys  # For exiting program
import threading  # Managing threads
import asyncio  # Asyncronous functions
import queue  # For thread_pool
import signal  # Allows async to gracefully exit


#################################################################
def parse_http(header, folder):
    code = "200 OK"
    try:
        first_space = header.index(" ")
        second_space = header.index(" ", first_space + 1)
        new_line = header.index("\r\n")
        end_of_header = header.index("\r\n\r\n", new_line)

        # Extract method, path, and version
        method = header[0:first_space]
        path = folder + header[first_space + 1 : second_space]
        version = header[second_space + 1 : new_line]
        host = header[new_line + 2 : end_of_header]

        logging.info(
            f"Method: '{method}', Path: '{path}', Version: '{version}', Host: '{host}'"
        )

        # Basic validation checks
        if not path or not version or not host:
            logging.error("400 Bad Request")
            code = "400 Bad Request"
        elif not os.path.isfile(path):
            logging.error(f"File does not exist: '{path}'")
            code = "404 Not Found"
            path = folder + "/404.html"
        elif method != "GET":
            code = "405 Method Not Allowed"

        file_size = os.path.getsize(path) if os.path.isfile(path) else 0

    except ValueError as e:
        logging.error(f"Malformed request: {e}")
        code = "400 Bad Request"
        path = None
        file_size = 0

    return {
        "code": code,
        "path": path,
        "file_size": file_size,
        "method": method,
        "version": version,
    }


#################################################################
def handle_client_threaded(conn, delay, folder):
    header = ""
    while True:
        header_byte = conn.recv(1)
        if not header_byte:
            logging.error("Header error...")
            break
        header += header_byte.decode()
        if header[-4:] == "\r\n\r\n":
            logging.info("Received header, parsing...")
            break

    # Parse the HTTP request
    result = parse_http(header, folder)
    code, path, file_size = result["code"], result["path"], result["file_size"]

    # Prepare and send the response
    response_header = f"HTTP/1.1 {code}\r\nContent-Length: {file_size}\r\n\r\n".encode()
    conn.send(response_header)

    if path and os.path.isfile(path):
        with open(path, "rb") as file:
            chunk_size = 1000
            while chunk := file.read(chunk_size):
                try:
                    conn.send(chunk)
                except BrokenPipeError:
                    logging.error("Client disconnected prematurely.")
                    break

    logging.info("Sent entire file, closing connection.")
    conn.close()  # Close the connection after sending


#################################################################
def worker_thread(connection_queue, delay, folder):
    while True:
        conn = connection_queue.get()  # Get a connection from the queue
        if conn is None:
            break  # If None is received, this thread will stop
        handle_client_threaded(conn, delay, folder)  # Handle the client
        connection_queue.task_done()  # Signal that task is complete


#################################################################
async def handle_client_async(reader, writer, delay, folder):
    address = writer.get_extra_info("peername")
    logging.info(f"Connection from: {address}")

    # Example: receive data from client asynchronously
    data = await reader.read(1024)  # Read up to 1024 bytes
    # logging.info(f"Received: {data.decode()}")

    # Parse the HTTP request
    result = parse_http(data.decode(), folder)
    code, path, file_size = result["code"], result["path"], result["file_size"]

    # Prepare and send the response
    response_header = f"HTTP/1.1 {code}\r\nContent-Length: {file_size}\r\n\r\n".encode()

    if delay:
        logging.info("Waiting for delay...")
        await asyncio.sleep(0.5)  # Simulate delay if needed
    writer.write(response_header)
    await writer.drain()  # Ensure data is sent before closing

    if path and os.path.isfile(path):
        with open(path, "rb") as file:
            chunk_size = 32
            while chunk := file.read(chunk_size):
                try:
                    writer.write(chunk)
                    await writer.drain()  # Ensure data is sent before closing
                except BrokenPipeError:
                    logging.error("Client disconnected prematurely.")
                    break

    logging.info("Closing the connection")
    writer.close()
    await writer.wait_closed()


#################################################################
def parse_arguments():
    parser = argparse.ArgumentParser(description="HTTP Server")

    # Adding arguments
    parser.add_argument("-p", "--port", type=int, default=8085, help="port to bind to")
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="turn on debugging output"
    )
    parser.add_argument(
        "-d",
        "--delay",
        action="store_true",
        default=0,
        help="add a delay for debugging purposes",
    )
    parser.add_argument(
        "-f", "--folder", type=str, default=".", help="folder from where to serve files"
    )
    parser.add_argument(
        "-c",
        "--concurrency",
        type=str,
        choices=["thread", "thread-pool", "async"],
        default="thread",
        help="concurrency methodology to use",
    )

    return parser.parse_args()


#################################################################
# Threaded server
#################################################################
def run_threaded_server(port, delay, folder):
    server_socket = socket.socket()
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind(("", port))
    server_socket.listen()

    try:
        while True:
            conn, address = server_socket.accept()
            logging.info(f"Connection from: {address}")
            # Create and start a new thread for each client
            client_thread = threading.Thread(
                target=handle_client_threaded, args=(conn, delay, folder)
            )
            client_thread.start()

    except KeyboardInterrupt:
        logging.info("Server shutting down...")
        server_socket.close()  # Close the socket
        sys.exit(0)  # Exit gracefully with code 0


#################################################################
# Asyncronous server
#################################################################
async def run_async_server(port, delay, folder):
    server = await asyncio.start_server(
        lambda r, w: handle_client_async(r, w, delay, folder), host="0.0.0.0", port=port
    )
    addr = server.sockets[0].getsockname()
    logging.info(f"Serving on {addr}")

    # Create an event to manage shutdown
    shutdown_event = asyncio.Event()

    # Function to handle graceful shutdown
    def shutdown():
        logging.info("Shutting down server gracefully...")
        server.close()  # Stop accepting new connections
        shutdown_event.set()  # Signal shutdown completion

    # Register the shutdown handler for SIGINT (Ctrl+C)
    loop = asyncio.get_event_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, shutdown)

    # Run the server and wait for the shutdown event
    async with server:
        await shutdown_event.wait()  # Wait for shutdown signal
        await server.wait_closed()  # Wait until server closes


#################################################################
# Threadpool server
#################################################################
def run_thread_pool_server(port, delay, folder, max_workers):
    # Set up the server socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind(("", port))
    server_socket.listen()
    logging.info(
        f"Server listening on port {port} with thread pool of size {max_workers}"
    )

    connection_queue = (
        queue.Queue()
    )  # Queue holds connections until there's an available thread

    # Begin all worker threads
    threads = []
    for _ in range(max_workers):
        thread = threading.Thread(
            target=worker_thread, args=(connection_queue, delay, folder)
        )
        thread.start()
        threads.append(thread)

    try:
        while True:  ###MAIN SERVER LOOP###
            conn, address = server_socket.accept()
            logging.info(f"Connection from: {address}")
            connection_queue.put(conn)  # Add connection to the queue
            ###Threads come and pick up a connection from the queue whenever they're available

    except KeyboardInterrupt:
        logging.info("Server shutting down...")

        # End all threads
        logging.info("Closing all threads...")
        for _ in threads:
            connection_queue.put(None)

        for thread in threads:
            thread.join()

        server_socket.close()  # Close the socket
        sys.exit(0)


#################################################################
# Decide which server to run
#################################################################
def run_server(port, delay, folder, concurrency):
    if concurrency == "thread":
        run_threaded_server(port, delay, folder)
    elif concurrency == "thread-pool":
        run_thread_pool_server(port, delay, folder, 10)
    elif concurrency == "async":
        try:
            asyncio.run(run_async_server(port, delay, folder))
        except KeyboardInterrupt:
            logging.info("Server stopped.")
            sys.exit(0)
    else:
        logging.error("Unknown concurrency mode.")
        sys.exit(1)


if __name__ == "__main__":
    args = parse_arguments()

    # Set the logging level based on the verbose flag
    log_level = logging.DEBUG if args.verbose else logging.ERROR
    logging.basicConfig(
        level=log_level,  # Set the log level based on the -v flag
        format="*%(levelname)s* (Line %(lineno)d) - %(message)s",
    )

    # Run the server with the provided port
    run_server(args.port, args.delay, args.folder, args.concurrency)
