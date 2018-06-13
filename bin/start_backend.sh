#!/usr/bin/env bash
set -euo pipefail

cd /opt/continuum/backends/python

celery -b redis://redis:6379 -A modules.worker worker -l info &>/var/log/celery.log &
sleep 5

python main.py --continuum-host data_frontend --app-name test-app
