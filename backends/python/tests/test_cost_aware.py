# coding: utf-8
import time

from utils import test_policy

if __name__ == '__main__':
    test_policy("test-app", [(10, 0.5), (16, 0.5), (12, 0.5), (7, 0.5),
                             (23, 0.4), (25, 0.5), (14, 0.5), (10, 1)])
