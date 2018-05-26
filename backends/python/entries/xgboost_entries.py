# coding: utf-8


def retrain(data):
    """
    /var/opt/xgboost
    |- 00000000000/
        |- model
        |- lock
    |- 11397283704/
    ...
    """
    import xgboost as xgb
    import numpy as np
    from os.path import abspath, dirname, join
    import sys
    sys.path.insert(0, join(dirname(dirname(abspath(__file__))), 'modules'))
    import utils
    sys.path.pop(0)

    XGBOOST_DIR = '/var/opt/xgboost'
    latest_dir = utils.get_latest_dir(XGBOOST_DIR)

    if latest_dir is not None:
        base_model = xgb.Booster()
        base_model.load_model('{}/model'.format(latest_dir))
    else:
        base_model = None

    # retrain model
    X = np.array([d[1:] for d in data])
    y = np.array([d[0] for d in data])
    dataset = xgb.DMatrix(X, label=y)
    params = {
        'max_depth': 5,
        'eta': 0.1,
        'objective': 'binary:logistic',
        'silent': 0
    }
    model = xgb.train(params, dataset, 30, [], xgb_model=base_model)

    # save model
    new_dir = utils.create_child_dir(XGBOOST_DIR)
    model.save_model('{}/model'.format(new_dir))

    import time
    time.sleep(10)
    utils.commit_dir(new_dir)


def fetch():
    pass


def main(rpc_service,
         continuum_host='localhost',
         continuum_port=7001,
         backend_name='xgboost',
         backend_version='1.0',
         backend_module='xgboost_entries',
         app_name='xgboost-app',
         policy_name='NaiveBestEffortPolicy',
         input_type='doubles',
         params={},
         **kwargs):
    params['alpha'] = params['alpha'] if 'alpha' in params else 4.0
    params['beta'] = params['beta'] if 'beta' in params else 10000.0
    rpc_service.start(continuum_host, continuum_port, backend_name, backend_version, \
                      backend_module, app_name, policy_name, input_type, params)
