# coding: utf-8
import os
import sys
from os.path import abspath, dirname, join
import importlib

from celery import Celery


redis_host = os.getenv('REDIS_HOST', 'localhost')
redis_port = os.getenv('REDIS_ENV', 6379)

app = Celery(
    'continuum-backend',
    broker='redis://{}:{}'.format(redis_host, redis_port),
    backend='redis://{}:{}'.format(redis_host, redis_port),
    task_acks_late=True)


def load_entries(name):
    sys.path.insert(0, join(dirname(dirname(abspath(__file__))), 'entries'))
    module = importlib.import_module(name, package='entries')
    sys.path.pop(0)
    return module


@app.task
def retrain(module_name, data):
    load_entries(module_name).retrain(data)


@app.task
def fetch(module_name):
    load_entries(module_name).fetch()
