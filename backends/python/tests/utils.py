# coding: utf-8
import json
import time

import requests
import numpy as np

FEATURE_NUM = 2
headers = {"Content-type": "application/json"}


def test_policy(app_name, size_delay_pairs):
    url = "http://0.0.0.0:1339/{app}/upload".format(app=app_name)

    for a, b in size_delay_pairs:
        print("send {data} data, sleep {time}s ".format(data=a, time=b))
        requests.post(
            url,
            headers=headers,
            data=json.dumps({
                "data": np.random.random((a, FEATURE_NUM)).tolist()
            }))
        time.sleep(b)


def send_retrain(app_name):
    url = "http://0.0.0.0:1339/{app}/retrain".format(app=app_name)
    requests.post(url, headers=headers)
