#!/bin/bash

set -eux

cd boost

./bootstrap.sh
BOOST_ARGS="cxxflags=-fPIC cflags=-fPIC --with-system --with-chrono --with-timer --with-thread --with-date_time --with-regex --with-serialization"
./b2 $BOOST_ARGS -d0 -j 6
./b2 $BOOST_ARGS install -j 6 -d0 --prefix="$PWD"

