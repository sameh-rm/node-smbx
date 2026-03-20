# node-smbx

Low-level TypeScript SMB binding backed by `libsmb2`.

## Platform Support

The package is intended to build and run on:

- Windows
- Linux
- macOS

Consumers currently build the native addon for their local platform during
install unless a matching prebuild is provided later.

## Status

This repository is being bootstrapped around a Node-API addon that wraps `libsmb2`.
The API is intentionally low-level: connect, stat, open/read/write/close,
truncate, directory operations, and metadata access with `bigint` file sizes and
offsets.

## Build

Prerequisites:

- Node.js `>=18.17.0`
- A working native build toolchain for Node addons
- Network access to an SMB server for integration testing

Platform notes:

- Windows: Visual Studio Build Tools for `node-gyp`
- Linux: GCC/Clang, `make`, and Python
- macOS: Xcode Command Line Tools and Python

Install dependencies and build:

```bash
npm install
npm run build
```

## API

The package exports `SmbConnection` plus error classes and shared types.

- File sizes and file offsets use `bigint`
- Read lengths use `number`
- Paths are share-relative and normalized to SMB-style separators
- The package is byte-transparent and does not implement application crypto

See `docs/usage.md` for working examples.

## Packaging

The repository root is the publishable package. Both local path installs and
tarball installs are intended to work once the native addon is built.

## Licensing

The distributed package contains:

- MIT-licensed wrapper code
- LGPL-2.1-or-later `libsmb2` code

See `LICENSE`, `LGPL-2.1-LICENSE`, and `LINKING.md`.
