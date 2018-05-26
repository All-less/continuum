# coding: utf-8
from __future__ import print_function


def retrain(data):
    print('scikit_entries.retrain called.')


def fetch():
    print('scikit_entries.fetch called.')


def main(rpc_service,
         continuum_host='localhost',
         continuum_port=7001,
         backend_name='scikit',
         backend_version='1.0',
         backend_module='scikit_entries',
         app_name='scikit-app',
         policy_name='NaiveBestEffortPolicy',
         input_type='doubles',
         params={},
         **kwargs):
    rpc_service.start(continuum_host, continuum_port, backend_name, backend_version, \
                      backend_module, app_name, policy_name, input_type, params)
