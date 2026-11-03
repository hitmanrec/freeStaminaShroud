# Enshwick C ABI header

Vendored unchanged from `enshwick/enshwick` commit
`535efb4944f99b84de6c5cd12378042b0485d3dc`, path
`crates/enshwick/include/enshwick.h` (ABI 11.1).

Upstream: https://github.com/enshwick/enshwick

MIT license: see LICENSE in this directory. The diagnostic needs a loader with
ABI major 11, minor at least 1 and a full matching HostApi table. A moving `dev`
release may eventually have another ABI; a rejection requires a reviewed SDK
update and rebuild, not disabling the compatibility checks.
