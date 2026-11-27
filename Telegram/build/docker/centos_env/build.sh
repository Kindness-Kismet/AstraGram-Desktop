#!/bin/bash
set -e

cd Telegram
./configure.sh -DASTRAGRAM_LINUX_BUNDLE_WIDGET_HELPER=ON "$@"
# KEEP_GOING=1 lets ninja report every failing translation unit in one
# run instead of stopping at the first; the exit status still fails.
cmake --build ../out --config "${CONFIG:-Release}" ${KEEP_GOING:+-- -k 0}
