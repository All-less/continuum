# coding: utf-8
"""
To use this backend,
    1. create /var/opt/ppr/{src,data}
    2. put code to /var/opt/ppr/src
    3. put graph file to /var/opt/ppr/data
"""
from __future__ import print_function


def retrain(data):
    """
    /var/opt/ppr/
    |- data/
        |- graph
        |- 11397283704/
        |- 11397283928/
    |- src/
    """
    PPR_DATA_DIR = '/var/opt/ppr/data'
    PPR_CODE_DIR = '/var/opt/ppr/src'

    import subprocess
    from os.path import abspath, dirname, join
    import sys
    sys.path.insert(0, join(dirname(dirname(abspath(__file__))), 'modules'))
    import utils
    sys.path.pop(0)

    latest_dir = utils.get_latest_dir(PPR_DATA_DIR)
    new_dir = utils.create_child_dir(PPR_DATA_DIR)

    with open('{}/edges'.format(new_dir), 'w') as f:
        for (u, v) in data:
            f.write('{} {}\n'.format(int(u), int(v)))

    if latest_dir is not None:
        cmd = '''
            cd {cwd} &&
            export SBT_OPTS="-Xmx8G -XX:+UseConcMarkSweepGC -XX:+CMSClassUnloadingEnabled -XX:MaxPermSize=8G" &&
            sbt "run retrain {prev_dir} {res_dir} edges {res_dir}" &>{res_dir}/log
            '''.format(
            cwd=PPR_CODE_DIR, prev_dir=latest_dir, res_dir=new_dir)
    else:
        cmd = '''
            cd {cwd} &&
            export SBT_OPTS="-Xmx8G -XX:+UseConcMarkSweepGC -XX:+CMSClassUnloadingEnabled -XX:MaxPermSize=8G" &&
            sbt "run train {graph_dir} graph 0.0015 0.05 89805 {res_dir} edges {res_dir}" &>{res_dir}/log
            '''.format(
            cwd=PPR_CODE_DIR, graph_dir=PPR_DATA_DIR, res_dir=new_dir)

    utils.execute_cmd(cmd)
    import time
    time.sleep(10)
    utils.commit_dir(new_dir)


def fetch():
    print('ppr_entries.fetch called.')


def main(rpc_service,
         continuum_host='localhost',
         continuum_port=7001,
         backend_name='ppr',
         backend_version='1.0',
         backend_module='ppr_entries',
         app_name='ppr-app',
         policy_name='NaiveBestEffortPolicy',
         input_type='doubles',
         params={},
         **kwargs):
    params['alpha'] = params['alpha'] if 'alpha' in params else 500.0
    params['beta'] = params['beta'] if 'beta' in params else 7500.0
    rpc_service.start(continuum_host, continuum_port, backend_name, backend_version, \
                      backend_module, app_name, policy_name, input_type, params)
