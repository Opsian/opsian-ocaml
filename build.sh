#!/bin/sh

set -eu

dune clean
dune build examples/opsian_examples.exe

