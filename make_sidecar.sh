#!/bin/bash
set -e
cd $HOME/sidecar/quiche/
make sidecar
cd $HOME/sidecar/curl/
autoreconf -fi
./configure LDFLAGS="-Wl,-rpath,$HOME/sidecar/quiche/target/release" \
    --with-openssl=$HOME/sidecar/quiche/quiche/deps/boringssl/src \
    --with-quiche=$HOME/sidecar/quiche/target/release
make -j4

