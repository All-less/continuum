# coding: utf-8
"""
To use this backend,
    1. create /var/opt/mf/{src,data}
    2. put code to /var/opt/mf/src
    3. put initial model to /var/opt/mf/data/0000000000
"""
from __future__ import print_function


def retrain(data):
    """
    /var/opt/mf/
    |- data/
        |- 0000000000/
            |- model
            |- lock
        |- 1523335358/
        ...
    |- src/
    """
    MF_DATA_DIR = '/var/opt/mf/data'
    MF_CODE_DIR = '/var/opt/mf/src'

    import subprocess
    import time
    from os.path import abspath, dirname, join
    import sys
    sys.path.insert(0, join(dirname(dirname(abspath(__file__))), 'modules'))
    import utils
    sys.path.pop(0)

    latest_dir = utils.get_latest_dir(MF_DATA_DIR)
    new_dir = utils.create_child_dir(MF_DATA_DIR)

    with open('{}/data'.format(new_dir), 'w') as f:
        for (user, item) in data:
            f.write('{}\t{}\t1.0\t00000000\n'.format(int(user), int(item)))

    # We will not create a new model every time and always use the initial base model
    # because the model takes too much space. This is the reason why we skip committing
    # and modify FastMF source code to skip saving.
    cmd = '''
        cd {cwd} &&
        mill FastMF.run retrain {prev_dir}/model {res_dir}/data {res_dir}/model &>{res_dir}/log
        '''.format(
        cwd=MF_CODE_DIR, prev_dir=latest_dir, res_dir=new_dir)

    utils.execute_cmd(cmd)
    time.sleep(10)
    # utils.commit_dir(new_dir)


def fetch():
    print('mf_entries.fetch called.')


def main(rpc_service,
         continuum_host='localhost',
         continuum_port=7001,
         redis_host='localhost',
         redis_port=6379,
         backend_name='mf',
         backend_version='1.0',
         backend_module='mf_entries',
         app_name='mf-app',
         policy_name='NaiveBestEffortPolicy',
         input_type='doubles',
         params={},
         **kwargs):
    params['alpha'] = params['alpha'] if 'alpha' in params else 500.0
    params['beta'] = params['beta'] if 'beta' in params else 7500.0
    rpc_service.start(continuum_host, continuum_port, backend_name, backend_version, redis_host, \
                      redis_port, backend_module, app_name, policy_name, input_type, params)
