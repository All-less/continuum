# coding: utf-8
import glob
import os
import time
import subprocess


def _mkdir(newdir):
    """
    works the way a good mkdir should :)
        - already exists, silently complete
        - regular file in the way, raise an exception
        - parent directory(ies) does not exist, make them as well
    """
    if type(newdir) is not str:
        newdir = str(newdir)
    if os.path.isdir(newdir):
        pass
    elif os.path.isfile(newdir):
        raise OSError("a file with the same name as the desired " \
                      "dir, '%s', already exists." % newdir)
    else:
        head, tail = os.path.split(newdir)
        if head and not os.path.isdir(head):
            _mkdir(head)
        if tail:
            os.mkdir(newdir)

def get_latest_dir(path):
    """
    Find the latest directory which contains a 'lock' file with '1' as content.
    """
    dirs = glob.glob('{}/*'.format(path))
    for dir_ in reversed(sorted(dirs)):
        lock_path = '{}/lock'.format(dir_)
        if not os.path.isfile(lock_path):
            continue
        with open(lock_path, 'r') as f:
            if f.read() == '1':
                return dir_
    return None


def create_child_dir(parent):
    """
    Create a directory with name of time.time() under the parent path.
    """
    path = '{}/{}'.format(parent, int(time.time()))
    _mkdir(path)
    return path


def commit_dir(path):
    with open('{}/lock'.format(path), 'w') as f:
        f.write('1')


def execute_cmd(cmd):
    try:
        subprocess.check_output(cmd, shell=True, stderr=subprocess.STDOUT,
                                preexec_fn=os.setsid)
    except subprocess.CalledProcessError as e:
        raise Exception('''
            Command: {}
            Exit code: {}
            Output: {}
            '''.format(e.cmd, e.returncode, e.output))
