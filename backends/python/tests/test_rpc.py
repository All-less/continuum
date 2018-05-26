# coding: utf-8
import sys
from os.path import dirname, abspath
sys.path.append(dirname(dirname(abspath(__file__))))
import struct
import logging
logging.basicConfig(level=logging.INFO)

import zmq

from modules.constants import *

ids = ["1", "2"]
msg_id = 0

identities = []
identity_backend = {}
backend_dict = {}

heartbeats_trigger = 3


def handle_message(msg_type, socket, sender_identity):
    global heartbeats_trigger
    logging.info('received message with type {}'.format(msg_type))
    if msg_type == MESSAGE_TYPE_BACKEND_HAERTBEAT:
        if sender_identity in identities:
            socket.send(sender_identity, flags=zmq.SNDMORE)
            socket.send('', flags=zmq.SNDMORE)
            socket.send(
                struct.pack('<I', MESSAGE_TYPE_BACKEND_HAERTBEAT),
                flags=zmq.SNDMORE)
            socket.send(struct.pack('<I', BACKEND_HEARTBEAT_TYPE_KEEPALIVE))

            # trigger retrain each 3 heartbeats
            if heartbeats_trigger == 0:
                retrain_trigger(socket, sender_identity)
                heartbeats_trigger = 5
            else:
                heartbeats_trigger -= 1

        else:
            # first time, require metadata
            identities.append(sender_identity)
            socket.send(sender_identity, flags=zmq.SNDMORE)
            socket.send('', flags=zmq.SNDMORE)
            socket.send(
                struct.pack('<I', MESSAGE_TYPE_BACKEND_HAERTBEAT),
                flags=zmq.SNDMORE)
            socket.send(
                struct.pack('<I', BACKEND_HEARTBEAT_TYPE_REQUEST_METADATA))

    elif msg_type == MESSAGE_TYPE_BACKEND_METADATA:
        backend_name = socket.recv()
        backend_version = socket.recv()
        app_name = socket.recv()
        policy = socket.recv()
        runtime_profile = socket.recv()

        logging.info('Received backend metadata : {}'.format(backend_name))
        identity_backend[sender_identity] = backend_name
        backend_dict[backend_name] = (backend_version, app_name)

    elif msg_type == MESSAGE_TYPE_RETRAIN_STARTED:
        msg_id_bytes = socket.recv()
        received_msg_id = int(struct.unpack("<I", msg_id_bytes)[0])
        logging.info('Retrain started in backend : {} with msg id : {}'.format(
            identity_backend[sender_identity], received_msg_id))

    elif msg_type == MESSAGE_TYPE_RETRAIN_ENDED:
        msg_id_bytes = socket.recv()
        received_msg_id = int(struct.unpack("<I", msg_id_bytes)[0])
        logging.info('Retrain ended in backend : {} with msg id : {}'.format(
            identity_backend[sender_identity], received_msg_id))


def retrain_trigger(socket, sender_identity):
    global msg_id
    logging.info('trigger retrain to backend {}'.format(
        identity_backend[sender_identity]))
    # ROUTER socket needs to specify sender identity first
    socket.send(sender_identity, flags=zmq.SNDMORE)
    socket.send('', flags=zmq.SNDMORE)
    socket.send(
        struct.pack('<I', MESSAGE_TYPE_START_RETRAIN), flags=zmq.SNDMORE)
    socket.send(struct.pack('<I', msg_id), flags=zmq.SNDMORE)
    socket.send(
        struct.pack('<I', REQUEST_TYPE_START_RETRAIN), flags=zmq.SNDMORE)
    socket.send(bytearray('\0'.join(ids) + '\0'))

    msg_id += 1


if __name__ == '__main__':

    context = zmq.Context()
    socket = context.socket(zmq.ROUTER)
    socket.bind("tcp://*:7001")

    while True:
        # ROUTER socket will receive routing identity first
        routing_identity = socket.recv()
        logging.debug('{!r}'.format(routing_identity))
        message_delimiter = socket.recv()  # an empty frame

        message = socket.recv()  # actual message
        logging.debug('{!r}'.format(message))
        msg_type = struct.unpack('<I', message)[0]

        handle_message(msg_type, socket, routing_identity)
