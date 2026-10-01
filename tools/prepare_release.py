"""Prepare version before compilation; user inputs are read as data, never shell code."""
import argparse
import os
import subprocess
from pathlib import Path
from firmware import json_write, prepare

parser = argparse.ArgumentParser()
parser.add_argument('--development', action='store_true')
args = parser.parse_args()
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
if args.development:
    request = dict(version=f'0.0.0-dev.{commit[:12]}', channel='development', commit=commit, hardware_tested=False)
else:
    request = prepare(os.environ['RELEASE_VERSION'], os.environ['RELEASE_CHANNEL'], commit,
                      os.environ.get('HARDWARE_TESTED') == 'true')
json_write('release-request.json', request)
Path('version.txt').write_text(request['version'] + '\n')
if os.environ.get('GITHUB_OUTPUT'):
    with open(os.environ['GITHUB_OUTPUT'], 'a') as f:
        f.write(f"tag=v{request['version']}\n")
