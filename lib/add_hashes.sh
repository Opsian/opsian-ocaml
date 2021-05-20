#!/bin/sh

set -eu

export GIT_HASH=$(git rev-parse HEAD)
export GIT_TAGS=$(git tag --points-at HEAD)

cp globals.cpp.in globals.cpp
sed -i "s/@GIT_HASH@/$GIT_HASH/;s/@GIT_TAGS@/$GIT_TAGS/;" globals.cpp

