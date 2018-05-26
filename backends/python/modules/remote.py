# coding: utf-8
from __future__ import absolute_import
import struct
from datetime import datetime
import logging

from celery import signals
from celery.task.control import revoke
import zmq
import gevent.monkey  # we need gevent to use 'then' on AsyncResult
gevent.monkey.patch_all()

from . import worker
from .state import env
from .constants import *

logger = logging.getLogger(__name__)


def on_retrain_finished(result):

    env['is_training'] = False
    if str(result.result) == 'terminated':
        return

    if env['socket'] is None:  # socket not created yet
        return
    socket = env['socket']  # maybe we need a lock when sending the message
    socket.send("", flags=zmq.SNDMORE)
    socket.send(
        struct.pack('<I', MESSAGE_TYPE_RETRAIN_ENDED), flags=zmq.SNDMORE)
    socket.send(struct.pack('<I', env['msg_id']), flags=zmq.SNDMORE)
    socket.send(struct.pack('<I', 1))

    logger.info(
        'Sent RETRAIN_ENDED for msg_id {}.'.format(env['msg_id']))


def trigger_retrain(backend_module, data):
    env['retrain_res'] = worker.retrain.delay(backend_module, data)
    env['retrain_res'].then(on_retrain_finished)
    env['is_training'] = True


def abort_retrain():
    env['retrain_res'].revoke(terminate=True)
    logger.info('Terminated training for msg_id {}.'.format(env['msg_id']))


def trigger_fetch(backend_module):
    env['fetch_res'] = worker.fetch.apply_async(backend_module)


# For how to use signal:
# http://docs.celeryproject.org/en/latest/userguide/signals.html
# Caveat: task_xxxx signals will be executed in worker process.
@signals.after_task_publish.connect
def after_task_publish_handler(**kwargs):

    if env['socket'] is None:  # not created yet
        return
    socket = env['socket']
    socket.send("", flags=zmq.SNDMORE)
    socket.send(
        struct.pack('<I', MESSAGE_TYPE_RETRAIN_STARTED), flags=zmq.SNDMORE)
    socket.send(struct.pack('<I', env['msg_id']), flags=zmq.SNDMORE)
    socket.send(struct.pack('<I', 1))

    logger.info(
        'Sent RETRAIN_STARTED for msg_id {}.'.format(env['msg_id']))
