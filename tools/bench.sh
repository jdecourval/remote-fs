#!/usr/bin/env bash

usage() {
  echo "$0 server client [endpoint]"
  echo server: Path to the server to benchmark
  echo client: Path to the client to benchmark
  echo endpoint: zmq compatible endpoint, e.g. tcp://127.0.0.1:8943. Default to a random unix socket.
  exit 0
}

[ $# != 2 ] && [ $# != 3 ] && { usage; }
endpoint=${3:-"ipc://$(mktemp)"}

temp=$(mktemp -d)
pushd "$temp" || exit 1
mkdir target mountpoint
fallocate -l 5G target/bigfile

cd target
/usr/bin/time -v "$1" "$endpoint" &
cd ..

/usr/bin/time -v "$2" -f mountpoint "$endpoint" &
sleep 2
dd if=mountpoint/bigfile of=/dev/null bs=1M
killall remote-fs-client remote-fs-server
sleep 2
fusermount -u mountpoint
rm -r target mountpoint
popd
rmdir "$temp"
