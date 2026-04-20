import paho.mqtt.client as mqtt
import argparse
import sys
import logging

# Flag to signal when to exit
exit_flag = False


# Callback when the client receives a response
def on_message(client, userdata, msg):
    global exit_flag
    print(msg.payload.decode())  # Print response to stdout
    exit_flag = True  # Set flag to True to signal the client to exit


def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        logging.info("Connected to MQTT broker")
    else:
        logging.error(f"Connection failed with code {rc}", file=sys.stderr)


def mqtt_client_main(args):
    global exit_flag
    # Initialize MQTT client
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = on_connect
    client.on_message = on_message

    # Connect to the broker
    client.connect(args.host, args.port, keepalive=60)

    # Setup topic names
    request_topic = f"{args.netid}/{args.action}/request"
    response_topic = f"{args.netid}/{args.action}/response"

    # Subscribe to the response topic
    client.subscribe(response_topic, qos=1)

    # Publish the message
    client.publish(request_topic, args.message, qos=1)

    # Start the loop to process network traffic asynchronously
    while not exit_flag:
        client.loop(1)  # Use loop(1) to process events in short intervals

    # After receiving the message, stop the loop and exit
    client.loop_stop()
    logging.info("Client exiting gracefully.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="MQTT client for BYU ECEN 426 lab.")
    parser.add_argument("netid", help="NetID for the topic and client ID")
    parser.add_argument(
        "action", help="Action to be performed (e.g., uppercase, lowercase)"
    )
    parser.add_argument("message", help="Message to send to the MQTT broker")
    parser.add_argument(
        "-p", "--port", type=int, default=1883, help="Port number for MQTT broker"
    )
    parser.add_argument(
        "--host", default="localhost", help="Hostname of the MQTT broker"
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Enable verbose output"
    )

    args = parser.parse_args()

    log_level = logging.DEBUG if args.verbose else logging.ERROR
    logging.basicConfig(
        level=log_level,  # Set the log level based on the -v flag
        format="*%(levelname)s* (Line %(lineno)d) - %(message)s",
    )

    action = args.action
    if (
        action == "uppercase"
        or action == "lowercase"
        or action == "reverse"
        or action == "shuffle"
        or action == "random"
    ):  # random
        mqtt_client_main(args)
    else:
        logging.error("Invalid action")
        sys.exit(1)
