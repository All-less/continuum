# coding: utf-8
from __future__ import absolute_import
import sys
import socket
import struct
import logging
import time
from datetime import datetime
import threading
import json

import gevent.monkey  # we need gevent to use 'then' on AsyncResult
gevent.monkey.patch_all()
import zmq.green as zmq
import numpy as np

from . import database
from . import remote
from .constants import *
from .state import env

logger = logging.getLogger(__name__)


class Server(threading.Thread):
    def __init__(self, context, continuum_ip, continuum_port):
        threading.Thread.__init__(self)
        self.context = context
        self.continuum_ip = continuum_ip
        self.continuum_port = continuum_port
        self.socket_state = STATE_DESTROYED
        self.last_active_time = datetime.now()
        self.connected = False
        self.poller = zmq.Poller()
        self._redis_service = database.RedisService(REDIS_HOST, REDIS_PORT)

    def run(self):
        logger.info('Start waiting for new messages.')
        continuum_address = 'tcp://{}:{}'.format(self.continuum_ip,
                                               self.continuum_port)
        while True:
            if self.socket_state == STATE_DESTROYED:
                # create a new socket
                self.socket = self.context.socket(zmq.DEALER)
                env['socket'] = self.socket
                self.socket.connect(continuum_address)
                self.poller.register(self.socket, zmq.POLLIN)
                self.socket_state = STATE_IDLE
            elif self.socket_state == STATE_IDLE:
                self.send_heartbeat()
                self.socket_state = STATE_RECEIVING
            elif self.socket_state == STATE_RECEIVING:
                # waiting for new message
                self.poll_socket()
            elif self.socket_state == STATE_RECEIVABLE:
                # new message is available
                self.recv_message()
                self.socket_state = STATE_RECEIVING

    def poll_socket(self):
        receivable_sockets = dict(
            self.poller.poll(SOCKET_POLLING_TIMEOUT_MILLIS))
        if self.socket not in receivable_sockets or \
            receivable_sockets[self.socket] != zmq.POLLIN:
            # polling timed out
            if self.connected:
                time_delta = datetime.now() - self.last_active_time
                time_delta_millis = (time_delta.seconds * 1000) + \
                                    (time_delta.microseconds / 1000)
                if time_delta_millis >= SOCKET_ACTIVITY_TIMEOUT_MILLIS:
                    # we haven't received message for long
                    logger.info('Connection timed out. Trying to reconnect ...')
                    self.connected = False
                    self.poller.unregister(self.socket)
                    self.socket.close()
                    # close current socket to reconnect
                    self.socket_state = STATE_DESTROYED
                else:
                    self.socket_state = STATE_IDLE
        else:
            # we have new message
            self.connected = True
            self.last_active_time = datetime.now()
            self.socket_state = STATE_RECEIVABLE

    def send_heartbeat(self):
        self.socket.send("", zmq.SNDMORE)
        self.socket.send(struct.pack("<I", MESSAGE_TYPE_BACKEND_HAERTBEAT))
        logger.debug('Sent backend heartbeat.')

    def send_metadata(self):
        self.socket.send("", zmq.SNDMORE)
        self.socket.send(
            struct.pack("<I", MESSAGE_TYPE_BACKEND_METADATA), zmq.SNDMORE)
        self.socket.send_string(self.backend_name, zmq.SNDMORE)
        self.socket.send_string(str(self.backend_version), zmq.SNDMORE)
        self.socket.send_string(self.app_name, zmq.SNDMORE)
        self.socket.send_string(self.policy, zmq.SNDMORE)
        self.socket.send_string(json.dumps(self.params))
        logger.info('Sent backend metadata.')

    def handle_heartbeat(self):
        logger.debug('Received heartbeat!')
        heartbeat_type_bytes = self.socket.recv()
        heartbeat_type = struct.unpack("<I", heartbeat_type_bytes)[0]
        if heartbeat_type == BACKEND_HEARTBEAT_TYPE_REQUEST_METADATA:
            self.send_metadata()

    def handle_start_retrain(self):
        msg_id_bytes = self.socket.recv()
        msg_id = int(struct.unpack("<I", msg_id_bytes)[0])
        request_header = self.socket.recv()
        request_type = struct.unpack("<I", request_header)[0]
        logger.info('Received START_RETRAIN with msg_id {}.'.format(msg_id))
        if request_type == REQUEST_TYPE_START_RETRAIN:
            retrain_content = self.socket.recv()
            batch_ids = np.array(
                retrain_content.split('\0')[:-1], dtype=np.str)
            logger.debug('Received batch ids {}'.format(batch_ids))
            data = self._redis_service.get_data_by_ids(batch_ids)
            if env['is_training']:
                remote.abort_retrain()
                time.sleep(3)  # give the process some time to clean up
            env['msg_id'] = msg_id
            remote.trigger_retrain(self.backend_module, data)

    def recv_message(self):
        self.socket.recv()  # skip the first empty frame
        msg_type_bytes = self.socket.recv()
        msg_type = struct.unpack('<I', msg_type_bytes)[0]
        logger.debug('Received message with type {}'.format(msg_type))
        if msg_type == MESSAGE_TYPE_BACKEND_HAERTBEAT:
            self.handle_heartbeat()
        elif msg_type == MESSAGE_TYPE_START_RETRAIN:
            self.handle_start_retrain()


class RPCService:
    def __init__(self):
        pass

    def start(self, host, port, backend_name, backend_version, \
              backend_module, app_name, policy, input_type, param_dict={}):
        """
        Args:
            host (str): The Continuum RPC hostname or IP address.
            port (int): The Continuum RPC port.
            backend_name (str): The name of training backend.
            input_type (str): One of ints, doubles, floats, bytes, strings.
        """
        try:
            ip = socket.gethostbyname(host)
        except socket.error as e:
            logger.error('Error resolving {}: {}'.format(host, e))
            sys.exit(1)
        context = zmq.Context()
        self.server = Server(context, ip, port)
        self.server.backend_name = backend_name
        self.server.backend_version = backend_version
        self.server.backend_module = backend_module
        self.server.policy = policy
        self.server.app_name = app_name
        self.server.input_type = input_type
        self.server.params = param_dict
        self.server.run()
