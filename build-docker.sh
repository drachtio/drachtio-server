#!/bin/sh

docker build --build-arg MYVERSION=$(git describe --always) . "$@"
