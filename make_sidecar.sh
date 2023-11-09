#!/bin/bash
set -e
export QUICHE_HOME=$HOME/sidecar/http3_integration/quiche
cd $QUICHE_HOME
make sidecar
cd $HOME/sidecar/curl/
autoreconf -fi
./configure LDFLAGS="-Wl,-rpath,$QUICHE_HOME/target/release" \
    --with-openssl=$QUICHE_HOME/quiche/deps/boringssl/src \
    --with-quiche=$QUICHE_HOME/target/release
make -j4

