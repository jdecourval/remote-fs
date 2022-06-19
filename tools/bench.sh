#!/usr/bin/env bash

directory=$(dirname "$(readlink -f "$0")")

usage() {
  echo "$0 scenario server client [endpoint]"
  echo "scenario: scenario to test. One of: $(ls $directory/scenarios | xargs)"
  echo server: Path to the server to benchmark
  echo client: Path to the client to benchmark
  echo endpoint: zmq compatible endpoint, e.g. tcp://127.0.0.1:8943. Default to a random unix socket.
  exit 0
}

[ $# != 3 ] && [ $# != 4 ] && { usage; }
source "$directory/scenarios/$1"
server="$2"
client="$3"
endpoint=${4:-"ipc://$(mktemp)"}

temp=$(mktemp -d)
pushd "$temp" || exit 1
mkdir target mountpoint
setup

cd target
/usr/bin/time -v "$server" "$endpoint" &
cd ..

/usr/bin/time -v "$client" -f mountpoint "$endpoint" &
sleep 2

benchmark
killall remote-fs-client remote-fs-server
sleep 1
fusermount -u mountpoint 2>/dev/null
rm -r target mountpoint
popd >/dev/null
rmdir "$temp"
