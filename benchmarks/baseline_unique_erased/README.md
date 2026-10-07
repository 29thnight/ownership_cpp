# Rejected erased unique-owner prototype

This is the exact frozen header used for the old 40-byte owner comparison. It is
not the current implementation or a substitute built from the new opt-in type.
The rejected prototype was not published. Its original source, tests, reports and
results are preserved separately; this directory contains only the reproduction
header, copied without modification.

SHA256 of `include/own/ownership.hpp`:
`778759120c5b2920c85b653f011444d116f2501e205f528755ccfc31d735cd28`

The benchmark compiles the same fresh workload source against this header and the
current header. All build outputs are written outside this frozen input folder.
The project root's MIT license applies to this header.
