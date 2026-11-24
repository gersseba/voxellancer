#!/bin/bash
# Builds and starts the client with an integrated local server. Wrapper for
# the platform-independent harness: cmake -DMODE=Run -DRUN=LocalServer -P tools/agent-check.cmake
set -euo pipefail

cd "$(dirname "$0")/.."
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

joined=""
for arg in "$@"; do
    joined="${joined:+$joined;}$arg"
done
exec cmake -DMODE=Run -DRUN=LocalServer "-DARGS=$joined" -P tools/agent-check.cmake
