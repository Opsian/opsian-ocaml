#!/bin/sh

set -eu

cd protobuf && ./generate && cd ..
dune build examples/opsian_examples.exe

