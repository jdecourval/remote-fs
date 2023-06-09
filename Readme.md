# remote-fs

A WIP remote file system based on fuse that leverages io_uring, SCTP, C++23 and multiple tricks and
hacks to reach good performance.

The components are:

- lib/remotefs  
  Most of the common logic is here.

- sources/client  
  The client used to mount the filesystem.

- sources/server  
  The server that exposes remote files.

- sources/test-client  
  A client that doesn't use fuse, but only send Ping messages as a benchmark.
