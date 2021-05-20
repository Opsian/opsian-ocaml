#!/bin/sh

set -eu

cd protobuf && ./generate && cd ..
# dune clean
# dune rules -m > Makefile
dune build examples/opsian_examples.exe

