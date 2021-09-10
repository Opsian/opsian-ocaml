#!/bin/sh

set -eu

dune clean
dune rules examples/opsian_examples.exe -m > Makefile
dune build examples/opsian_examples.exe

