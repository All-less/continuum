# coding: utf-8
import time

from utils import test_policy, send_retrain

if __name__ == '__main__':
    test_policy("test-app", [(6, 3), (10, 0.2), (5, 1)])
    send_retrain("test-app")
    time.sleep(2)
    send_retrain("test-app")
    test_policy("test-app", [(4, 1), (2, 0.2), (25, 2)])
    send_retrain("test-app")
