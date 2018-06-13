# coding: utf-8
"""
To use this backend,
    1. create /var/opt/mallet/{tool,data}
    2. download mallet to /var/opt/mallet/tool
    3. put dictionary file to /var/opt/mallet/data
"""


def retrain(data):
    """
    /var/opt/mallet/
    |- tool/
    |- data/
        |- dictionary
        |- 11397283704/
            |- text
            |- data
            |- model
        |- 11397283928/
    """
    MALLET_BIN = '/var/opt/mallet/tool/bin/mallet'
    MALLET_DATA_DIR = '/var/opt/mallet/data'

    import subprocess
    from os.path import abspath, dirname, join
    import sys
    sys.path.insert(0, join(dirname(dirname(abspath(__file__))), 'modules'))
    import utils
    sys.path.pop(0)

    latest_dir = utils.get_latest_dir(MALLET_DATA_DIR)
    new_dir = utils.create_child_dir(MALLET_DATA_DIR)
    text_path = '{}/text'.format(new_dir)
    data_path = '{}/data'.format(new_dir)
    model_path = '{}/model'.format(new_dir)
    dict_path = '{}/dictionary'.format(MALLET_DATA_DIR)

    with open('{}/text'.format(new_dir), 'w') as f:
        for chars in data:
            f.write(''.join(map(lambda d: chr(int(d)), chars)) + '\n')

    utils.execute_cmd("cp {} {}".format(dict_path, new_dir))
    dict_path = '{}/dictionary'.format(new_dir)

    utils.execute_cmd((
        "{bin} import-file --input {input} --output {output} --token-regex '[\p{{L}}\p{{P}}]+' "
        "--keep-sequence --remove-stopwords --use-pipe-from {dictionary} "
    ).format(
        bin=MALLET_BIN,
        input=text_path,
        output=data_path,
        dictionary=dict_path))

    utils.execute_cmd((
        "{bin} train-topics --input {input} --num-topics 10 --output-model {model} "
        "--num-iterations 1000 --show-topics-interval 1000000 {base_model}"
    ).format(
        bin=MALLET_BIN,
        input=data_path,
        model=model_path,
        base_model=('' if latest_dir is None else
                    '--input-model {}/model'.format(latest_dir))))
    import time
    time.sleep(10)
    utils.commit_dir(new_dir)


def fetch():
    pass


def main(rpc_service,
         continuum_host='localhost',
         continuum_port=7001,
         redis_host='localhost',
         redis_port=6379,
         backend_name='mallet',
         backend_version='1.0',
         backend_module='mallet_entries',
         app_name='mallet-app',
         policy_name='NaiveBestEffortPolicy',
         input_type='doubles',
         params={},
         **kwargs):
    params['alpha'] = params['alpha'] if 'alpha' in params else 24.0
    params['beta'] = params['beta'] if 'beta' in params else 12000.0
    rpc_service.start(continuum_host, continuum_port, backend_name, backend_version, \
                      backend_module, app_name, policy_name, input_type, params)
