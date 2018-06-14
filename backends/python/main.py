# coding: utf-8
import logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s %(levelname)s %(message)s'
)
import importlib
import argparse
import pkgutil
from os.path import abspath, dirname, join

from modules.rpc import RPCService
from modules import remote


def get_args():
    parser = argparse.ArgumentParser()
    modules = pkgutil.iter_modules(path=[
        join(dirname(abspath(__file__)), 'entries')
    ])
    backend_names = [name for _, name, _ in modules ]
    parser.add_argument(
        '--backend-module', type=str, default='test_entries', choices=backend_names)
    parser.add_argument('--continuum-host', type=str)
    parser.add_argument('--continuum-port', type=int)
    parser.add_argument('--redis-host', type=str)
    parser.add_argument('--redis-port', type=int)
    parser.add_argument('--backend-version', type=str)
    parser.add_argument('--backend-name', type=str)
    parser.add_argument('--app-name', type=str)
    policy_names = ['SpeculativeBestEffortPolicy', 'CostAwarePolicy',
                    'NaiveBestEffortPolicy', 'ManualPolicy']
    parser.add_argument('--policy-name', type=str, choices=policy_names)
    parser.add_argument('--alpha', type=float)
    parser.add_argument('--beta', type=float)
    parser.add_argument('--weight', type=float)
    return parser.parse_args()


def copy_keys(src_dict, dst_dict, keys):
    for key in keys:
        if src_dict[key] is not None:
            dst_dict[key] = src_dict[key]


def process_args(args):
    params = {}
    arg_vars = vars(args)
    copy_keys(arg_vars, params, ['alpha', 'beta', 'weight'])
    kwargs = {}
    copy_keys(arg_vars, kwargs, ['backend_module', 'backend_name', 'backend_version',
                                 'app_name', 'policy_name', 'continuum_host',
                                 'continuum_port', 'redis_host', 'redis_port'])
    if params:
        kwargs['params'] = params
    return kwargs


if __name__ == '__main__':
    rpc = RPCService()
    args = get_args()
    backend = importlib.import_module('entries.{}'.format(args.backend_module))
    backend.main(rpc, **process_args(args))
