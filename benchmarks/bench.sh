#!/usr/bin/env bash

directory=$(dirname "$(readlink -f "$0")")

usage() {
  echo "$0 scenario server client [endpoint]"
  echo "scenario: scenario to test. One of: $(ls $directory/scenarios | xargs)"
  echo server: Path to the server to benchmark
  echo client: Path to the client to benchmark
  echo endpoint: e.g. 127.0.0.1.
  exit 0
}

[ $# != 3 ] && [ $# != 4 ] && { usage; }
source "$directory/scenarios/$1"
server="$2"
client="$3"

temp=$(mktemp -d)
pushd "$temp" > /dev/null || exit 1
mkdir target mountpoint
setup
endpoint_write=${endpoint_write:-"127.0.0.1"}
endpoint_read=${endpoint_read:-"127.0.0.1"}

cd target
/usr/bin/time -v perf stat "$server" --metrics "$endpoint_read" &
cd ..

/usr/bin/time -v perf stat "$client" -o max_read=8159 -f mountpoint "$endpoint_write" &
sleep 2

benchmark
killall remote-fs-client remote-fs-server socat 2> /dev/null
sleep 1
killall -s KILL remote-fs-client remote-fs-server socat 2> /dev/null
fusermount -u mountpoint 2>/dev/null
rm -r target mountpoint
popd >/dev/null
if [[ $endpoint_read == ipc* ]]; then rm "$(sed 's/ipc:\/\///' <<< "$endpoint_read")"; fi
rmdir "$temp"
exit 0
