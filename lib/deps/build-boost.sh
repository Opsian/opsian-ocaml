#!/bin/bash

set -eux

cd boost

# fix compilation bug on arch / fedora
sed -i 's/if PTHREAD_STACK_MIN > 0/ifdef PTHREAD_STACK_MIN/' libs/thread/include/boost/thread/pthread/thread_data.hpp

./bootstrap.sh
BOOST_ARGS="cxxflags=-fPIC cflags=-fPIC --with-system --with-chrono --with-timer --with-thread --with-date_time --with-regex --with-serialization"
./b2 $BOOST_ARGS -d0 -j 6
./b2 $BOOST_ARGS install -j 6 -d0 --prefix="$PWD"

