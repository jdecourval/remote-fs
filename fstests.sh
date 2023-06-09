#!/usr/bin/env bash
set -xe
set -o pipefail

usage() {
  echo "$0 server client [endpoint]"
  echo server: Path to the server to test
  echo client: Path to the client to test
  echo endpoint: e.g. 127.0.0.1.
  exit 0
}

[ $# != 2 ] && [ $# != 3 ] && { usage; }
server="$1"
client="$2"
endpoint=${3:-"127.0.0.1"}

tempdir="$(mktemp -d)"
export TEST_DEV="$tempdir/dev"
server_mount="$tempdir/server-mount"
export TEST_DIR="$tempdir/client-mount"

pushd "$tempdir" > /dev/null || exit 1

onexit() {
  popd >/dev/null
  killall remote-fs-client remote-fs-server 2> /dev/null || true
  sleep 1
  killall -s KILL remote-fs-client remote-fs-server 2> /dev/null || true
  fusermount -u "$TEST_DIR" 2>/dev/null || true
  sudo umount "$server_mount" 2>/dev/null || true

  rm -rf "$tempdir"
}

trap onexit EXIT

fallocate -l 10G "$TEST_DEV"
mkfs.xfs "$TEST_DEV"

mkdir -p "$server_mount"
sudo mount "$TEST_DEV" "$server_mount"

pushd "$server_mount" > /dev/null || exit 1
"$server" "$endpoint" &
popd > /dev/null

mkdir -p "$TEST_DIR"
"$client" -f "$TEST_DIR" -o max_read=65422 $endpoint &

sleep 200

./xfstests-dev/check -n -g quick

#export SCRATCH_DEV=/dev/loop1
#export SCRATCH_MNT=/mnt/scratch
