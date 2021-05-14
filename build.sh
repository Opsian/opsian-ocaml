#!/bin/sh

set -eu

dune clean
dune rules -m > Makefile
dune build examples/opsian_examples.exe

