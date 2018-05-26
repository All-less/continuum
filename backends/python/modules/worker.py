# coding: utf-8
import sys
from os.path import abspath, dirname, join
import importlib

from celery import Celery

app = Celery(
    'continuum-backend', 
    broker='redis://localhost', 
    backend='redis://',
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
