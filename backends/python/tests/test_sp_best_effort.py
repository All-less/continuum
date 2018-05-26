# coding: utf-8
import time

from utils import test_policy

if __name__ == '__main__':
    test_policy("test-app", [(6, 8), (5, 0.2), (2, 8),
                             (4, 8), (2, 0.2), (25, 8)])
    time.sleep(10)
    test_policy("test-app", [(2, 8), (3, 8), (4, 8),
                             (6, 8), (2, 0.2), (20, 8)])
