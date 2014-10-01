#!/bin/bash -e

mkdir -p m4 # seems to be needed when building from a clean checkout.
autoreconf --verbose --force --install --make || {
    echo 'autogen.sh failed';
    exit 1;
}

