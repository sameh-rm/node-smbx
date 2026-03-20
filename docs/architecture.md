# Architecture

`node-smbx` is a single-package repository:

- `src/` contains the TypeScript surface
- `native/` contains the Node-API addon
- `deps/libsmb2/` contains the vendored `libsmb2` source

The addon owns a `libsmb2` context per `SmbConnection` instance and drives it
from the Node event loop using `uv_poll`.
