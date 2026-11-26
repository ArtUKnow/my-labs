#!/bin/sh
set -eu
cd "$(dirname "$0")"
exec ./run.sh "${1:-test}"
