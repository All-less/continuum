# coding: utf-8
import time

from utils import test_policy

if __name__ == '__main__':
    test_policy("test-app", [(6, 8), (10, 0.2), (5, 8),
                             (4, 8), (2, 0.2), (25, 8)])
