# coding: utf-8
from __future__ import print_function


def retrain(data):
    import time
    sleep_t = 100 * len(data) + 1500
    time.sleep((sleep_t + 0.0) / 1000)
    print('test_entries.retrain called with retrain data : ' + str(data))


def fetch():
    print('test_entries.fetch called.')


def main(rpc_service,
         continuum_host='localhost',
         continuum_port=7001,
         redis_host='localhost',
         redis_port=6379,
         backend_name='test',
         backend_version='1.0',
         backend_module='test_entries',
         app_name='test-app',
         policy_name='NaiveBestEffortPolicy',
         input_type='doubles',
         params={'alpha': 100.0, 'beta': 1500.0, 'weight': 10.0},
         **kwargs):
    """
    To run the test, add application with following command first:
        curl -X POST --header "Content-Type:application/json" \
             -d '{"name":"test-app", "input_type":"doubles", "default_output":"-1.0", "latency_slo_micros":100000 }' \
             http://0.0.0.0:1338/admin/add_app

    Policies can be one of:
        SpeculativeBestEffortPolicy
        CostAwarePolicy
        NaiveBestEffortPolicy
        ManualPolicy
    """
    rpc_service.start(continuum_host, continuum_port, backend_name, backend_version, redis_host, \
                      redis_port, backend_module, app_name, policy_name, input_type, params)
