#!/bin/bash

set -eux

SCRIPT_HASH=$(md5sum download_and_build.sh | cut -d ' ' -f 1)
CURRENT_DIR=$(pwd)
TMP_DIR=$(mktemp -d)
cd "$TMP_DIR"

PROTO_VER=${PROTO_VER:-3.5.1}
PROTOBUF="protobuf-all-$PROTO_VER.tar.gz"

BOOST_VER=1.69.0
BOOST_VER_DIR=1_69_0
BOOST="boost_${BOOST_VER_DIR}.tar.gz"

OPENSSL=openssl-1.1.0k.tar.gz

# Protobuf
if [ ! -d "$CURRENT_DIR/protobuf" ] || ([ ! -e "$CURRENT_DIR/protobuf/script_hash" ] || [ $(< "$CURRENT_DIR/protobuf/script_hash" ) != "$SCRIPT_HASH" ]); then
	rm -rf protobuf
	mkdir protobuf
	cd protobuf
  # old ubuntu can't access github
  if [ ! -e "/build/$PROTOBUF" ]
  then
    wget -nv "https://github.com/protocolbuffers/protobuf/releases/download/v${PROTO_VER}/${PROTOBUF}"
  else
    cp "/build/$PROTOBUF" .
  fi
	tar -zxpf "$PROTOBUF"
	cd "protobuf-$PROTO_VER"
  # disable C++ 11
  sed -i 's/#define LANG_CXX11 1//' src/google/protobuf/stubs/port.h
	./configure "CFLAGS=-fPIC" "CXXFLAGS=-fPIC" --disable-shared --prefix="$CURRENT_DIR/protobuf"
	make -s -j6 && make install
  echo "$SCRIPT_HASH" > "$CURRENT_DIR/protobuf/script_hash"
  cd ..
fi

# OpenSSL
if [ ! -d "$CURRENT_DIR/openssl" ] || ([ ! -e "$CURRENT_DIR/openssl/script_hash" ] || [ $(< "$CURRENT_DIR/openssl/script_hash" ) != "$SCRIPT_HASH" ]); then
	rm -rf openssl
	mkdir openssl
	cd openssl
  # old ubuntu can't access github
  if [ ! -e "/build/$OPENSSL" ]
  then
    wget -nv "https://www.openssl.org/source/$OPENSSL"
  else
    cp "/build/$OPENSSL" .
  fi
	tar -zxpf "$OPENSSL"
	cd openssl-1.1.0k
    export CFLAG=-fPIC
	./config no-shared --prefix="$CURRENT_DIR/openssl" --openssldir="$CURRENT_DIR/openssl"
	make -s && make install
    echo "$SCRIPT_HASH" > "$CURRENT_DIR/openssl/script_hash"
	cd ..
  cd ..
fi

# Boost
if [ ! -d "$CURRENT_DIR/boost" ] || ([ ! -e "$CURRENT_DIR/boost/script_hash" ] || [ $(< "$CURRENT_DIR/boost/script_hash" ) != "$SCRIPT_HASH" ]); then
    rm -rf boost
    mkdir boost
    cd boost
    if [ ! -e "/build/$BOOST" ]
    then
      wget -nv  "https://boostorg.jfrog.io/artifactory/main/release/$BOOST_VER/source/$BOOST"
    else
      cp "/build/$BOOST" .
    fi
    tar xzf "$BOOST"
    cd "boost_${BOOST_VER_DIR}/"
    ./bootstrap.sh
    BOOST_ARGS="link=static runtime-link=static cxxflags=-fPIC cflags=-fPIC --with-system --with-chrono --with-timer --with-thread --with-date_time --with-regex --with-serialization"
    ./b2 $BOOST_ARGS -d0 -j 6
    ./b2 $BOOST_ARGS install -j 6 -d0 --prefix="$CURRENT_DIR/boost" && echo "$SCRIPT_HASH" > "$CURRENT_DIR/boost/script_hash"
fi

rm -rf "$TMP_DIR"

