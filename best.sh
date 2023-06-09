#!/bin/bash

fusermount -u /home/jerome/Prog/Cpp/remote-fs/mountpoint || true

pushd /home/jerome/Prog/Cpp/remote-fs/sandbox || exit
../build/release-clang/sources/server/remote-fs-server -vv --metrics --register-ring --threads 5 --batch-wait-timeout 1000 127.0.0.1 &
popd || exit

./build/release-clang/sources/client/remote-fs-client -o max_read=1048576,auto_unmount -f mountpoint 127.0.0.1 &
sleep 1
cat mountpoint/test.10G | pv | wc -c
killall remote-fs-server
killall remote-fs-client
