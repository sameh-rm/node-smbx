# node-smbx

A low-level, async SMB2/3 client for Node.js, backed by [libsmb2](https://github.com/sahlberg/libsmb2). Provides direct file and directory operations over the SMB protocol without streams, connection pools, or high-level abstractions.

[![Node.js](https://img.shields.io/badge/node-%3E%3D18.17.0-brightgreen)](https://nodejs.org/)
[![License](https://img.shields.io/badge/license-MIT%20%2B%20LGPL--2.1-blue)](#licensing)

## Features

- **Async/await API** — All SMB operations return promises and integrate with the Node.js event loop via `uv_poll` (no thread pool).
- **Full file I/O** — `open`, `read`, `write`, `close`, `ftruncate` with `bigint` offsets for large file support (>4 GiB).
- **Directory operations** — `stat`, `mkdir`, `rmdir`, `unlink`, `rename`, `opendir`, `readdir`, `closedir`.
- **Typed errors** — Native NT status codes are mapped to descriptive JavaScript error classes (`SmbAuthenticationError`, `SmbNotFoundError`, `SmbTimeoutError`, etc.).
- **SMB signing** — Configurable per connection (`"required"` or `"auto"`).
- **Stall diagnostics** — `getDebugState()` exposes pending operations with duration and per-operation metadata for troubleshooting.
- **Cross-platform** — Builds on Windows, Linux, and macOS.
- **NTLMv2 authentication** — Used by default for all connections.

## Table of Contents

- [Installation](#installation)
- [Quick Start](#quick-start)
- [API Overview](#api-overview)
  - [Connecting](#connecting)
  - [File Operations](#file-operations)
  - [Directory Operations](#directory-operations)
  - [Diagnostics](#diagnostics)
- [Path Rules](#path-rules)
- [Error Handling](#error-handling)
- [Building from Source](#building-from-source)
- [Testing](#testing)
- [Architecture](#architecture)
- [Licensing](#licensing)

## Installation

```bash
npm install node-smbx
```

The native addon compiles from vendored `libsmb2` source during install. No matching prebuild is required — `node-gyp-build` falls back to `node-gyp rebuild` automatically.

### Prerequisites

| Platform | Requirements |
|----------|-------------|
| **Windows** | Visual Studio Build Tools (for `node-gyp`), Python |
| **Linux** | GCC or Clang, `make`, Python |
| **macOS** | Xcode Command Line Tools, Python |

Node.js **>=18.17.0** is required (Node-API version 9).

## Quick Start

```ts
import { SmbConnection, O_RDONLY } from "node-smbx";

let connection: SmbConnection | undefined;

try {
  connection = await SmbConnection.connect({
    server: "fileserver",
    share: "documents",
    username: "alice",
    password: process.env.SMB_PASSWORD ?? "",
  });

  // Stat a file
  const info = await connection.stat("reports/quarterly.pdf");
  console.log(info.name, info.size, info.modifiedAt);

  // Read the first 4 KiB
  const handle = await connection.open("reports/quarterly.pdf", O_RDONLY);
  try {
    const chunk = await connection.read(handle, 0n, 4096);
    console.log(`Read ${chunk.length} bytes`);
  } finally {
    await connection.close(handle);
  }
} finally {
  if (connection?.connected) {
    await connection.disconnect();
  }
}
```

## API Overview

### Connecting

```ts
import { SmbConnection } from "node-smbx";

const connection = await SmbConnection.connect({
  server: "host",           // SMB server hostname or IP
  share: "share-name",      // Share name
  username: "user",         // Required
  password: "pass",         // Required (can be empty string)
  domain: "DOMAIN",         // Optional
  port: 445,                // Optional (default: 445)
  signing: "required",      // Optional: "required" | "auto" (default: "auto")
  timeoutSec: 30,           // Optional (default: 30). 0 disables timeout.
});
```

| Property | Type | Description |
|----------|------|-------------|
| `connected` | `boolean` | Whether the connection is active |
| `getMaxReadSize()` | `number` | Server-negotiated maximum read chunk size |
| `getMaxWriteSize()` | `number` | Server-negotiated maximum write chunk size |

Always disconnect in a `finally` block:

```ts
try {
  // ... operations ...
} finally {
  if (connection?.connected) {
    await connection.disconnect();
  }
}
```

### File Operations

| Method | Signature | Description |
|--------|-----------|-------------|
| `stat` | `(path: string) => Promise<StatResult>` | Get file/directory metadata |
| `open` | `(path: string, flags: number) => Promise<FileHandle>` | Open a file handle |
| `read` | `(handle, offset: bigint, length: number) => Promise<Buffer>` | Read bytes at offset |
| `write` | `(handle, data: Buffer\|Uint8Array, offset: bigint) => Promise<number>` | Write bytes at offset |
| `ftruncate` | `(handle, length: bigint) => Promise<void>` | Resize an open file |
| `close` | `(handle) => Promise<void>` | Close a file handle |
| `rename` | `(oldPath: string, newPath: string) => Promise<void>` | Rename a file or directory |
| `unlink` | `(path: string) => Promise<void>` | Delete a file |

**File open flags:** `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_EXCL`, `O_TRUNC` (re-exported from the native layer).

Offsets and file sizes use `bigint` for files larger than 4 GiB. Read `length` is a `number` since Node.js buffers are sized with numbers.

### Directory Operations

| Method | Signature | Description |
|--------|-----------|-------------|
| `mkdir` | `(path: string) => Promise<void>` | Create a directory |
| `rmdir` | `(path: string) => Promise<void>` | Remove an empty directory |
| `opendir` | `(path: string) => Promise<DirHandle>` | Open a directory for iteration |
| `readdir` | `(handle) => Promise<DirEntry \| null>` | Read the next directory entry (returns `null` at end) |
| `closedir` | `(handle) => Promise<void>` | Close a directory handle |

```ts
const dirHandle = await connection.opendir("folder1");
try {
  for (;;) {
    const entry = await connection.readdir(dirHandle);
    if (entry === null) break;
    if (entry.name === "." || entry.name === "..") continue;
    console.log(entry.name, entry.isDirectory, entry.size);
  }
} finally {
  await connection.closedir(dirHandle);
}
```

### Diagnostics

```ts
const state = connection.getDebugState();
// { connected, disconnecting, timeoutSec, pendingOperations[] }
```

Each pending operation includes `action`, `path`, `durationMs`, and a human-readable `summary` for troubleshooting stalls or timeouts.

### `StatResult`

```ts
{
  path: string;
  name: string;
  size: bigint;
  isDirectory: boolean;
  isSymlink: boolean;
  createdAt?: Date;
  modifiedAt?: Date;
  accessedAt?: Date;
}
```

## Path Rules

- Paths are **share-relative** — do not include the server or share name.
- Both `/` and `\` are accepted; they are normalized internally to `\`.
- Leading slashes/backslashes are stripped.
- Use `""` (empty string) to refer to the share root.

```ts
// All equivalent:
await connection.stat("folder1/report.pdf");
await connection.stat("/folder1/report.pdf");
await connection.stat("\\folder1\\report.pdf");
```

## Error Handling

Native errors are mapped to typed JavaScript error classes:

| Error Class | Code | When |
|-------------|------|------|
| `SmbConnectionError` | `CONNECTION` | Network/transport failures |
| `SmbAuthenticationError` | `AUTH` | Bad credentials or logon failure |
| `SmbSigningRequiredError` | `SIGNING_REQUIRED` | Signing required but server doesn't support it |
| `SmbProtocolError` | `PROTOCOL` | NT status error from server |
| `SmbTimeoutError` | `TIMEOUT` | Operation or connection timed out |
| `SmbNotFoundError` | `NOT_FOUND` | File or directory not found |
| `SmbAlreadyExistsError` | `ALREADY_EXISTS` | Target already exists |
| `SmbInvalidStateError` | `INVALID_STATE` | Invalid handle, argument, or connection state |

All errors extend `SmbError` and carry additional metadata:

```ts
try {
  await connection.stat("missing.pdf");
} catch (error) {
  if (error instanceof SmbNotFoundError) {
    console.error(error.code);       // "NOT_FOUND"
    console.error(error.path);       // "missing.pdf"
    console.error(error.nterror);    // NT status code
    console.error(error.operation);  // "stat"
    console.error(error.durationMs); // How long the op ran
  }
}
```

## Building from Source

```bash
git clone https://github.com/sameh-rm/node-smbx.git
cd node-smbx
npm install        # installs deps + compiles native addon
npm run build      # clean + TypeScript + native
```

Individual steps:

```bash
npm run clean          # Remove dist/ and build/
npm run build:ts       # Compile TypeScript → dist/
npm run build:native   # Compile native addon → build/Release/smbx.node
```

## Testing

### Unit Tests

```bash
npm test
```

Unit tests cover path normalization, error mapping, and the TypeScript API surface. They do not require a network.

### Integration Tests

Integration tests require a live SMB server. Set environment variables and run:

```bash
SMB_SERVER=host \
SMB_SHARE=share-name \
SMB_USERNAME=user \
SMB_PASSWORD=pass \
npm test
```

For read-only shares:

```bash
SMB_READ_ONLY=1 SMB_READ_PATH=/ npm test
```

## Architecture

```
node-smbx/
├── src/               TypeScript public API and error mapping
│   ├── index.ts       SmbConnection class, validation, re-exports
│   ├── errors.ts      Typed error classes + mapNativeError()
│   ├── types.ts       ConnectOptions, StatResult, DirEntry, handles
│   ├── native.ts      node-gyp-build loader
│   └── path.ts        Path normalization (/ → \, strip leading \)
├── native/            Node-API C++ addon
│   ├── addon.cc       Module entry point, O_ constants
│   ├── smb_connection.h/.cc  SmbConnectionWrap (Napi::ObjectWrap)
│   └── config.h       libsmb2 build configuration
├── vendor/libsmb2/    Vendored libsmb2 C source (LGPL-2.1)
├── test/
│   ├── unit/          Offline unit tests
│   └── integration/   Live SMB server tests
└── docs/              Extended documentation
```

Each `SmbConnection` owns a single `libsmb2` context. The native addon integrates with Node.js via `uv_poll` on the libsmb2 socket file descriptor — no thread pool is used. A 100ms service timer drives background protocol work (keepalives, timeouts).

## Licensing

This package contains two licenses:

- **MIT** — The TypeScript wrapper and native addon code in `src/` and `native/`.
- **LGPL-2.1-or-later** — The vendored `libsmb2` library in `vendor/libsmb2/`.

The `libsmb2` source is shipped in the npm package so consumers can relink against a modified version per LGPL requirements. See [LINKING.md](LINKING.md) for relinking instructions.

Full license texts: [LICENSE](LICENSE), [LGPL-2.1-LICENSE](LGPL-2.1-LICENSE).
