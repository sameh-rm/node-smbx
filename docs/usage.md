# Usage Guide

`node-smbx` is a low-level SMB client for Node.js. It gives you direct file and
directory operations over SMB without adding streams, pooling, or high-level
abstractions on top.

## Install

From npm:

```bash
npm install node-smbx
```

From a local checkout:

```bash
npm install ../node-smbx
```

## Import

```ts
import {
  O_CREAT,
  O_RDONLY,
  O_RDWR,
  O_TRUNC,
  SmbConnection,
  SmbNotFoundError
} from "node-smbx";
```

## Connect

All paths are share-relative. Do not include the server or share name in the
path you pass to file methods.

```ts
import { SmbConnection } from "node-smbx";

const connection = await SmbConnection.connect({
  server: "host",
  share: "sample-files",
  username: "smb",
  password: "password",
  domain: "host"
});
```

If you do not need a domain, omit it:

```ts
const connection = await SmbConnection.connect({
  server: "fileserver",
  share: "documents",
  username: "alice",
  password: process.env.SMB_PASSWORD ?? ""
});
```

Always disconnect in `finally`:

```ts
let connection: SmbConnection | undefined;

try {
  connection = await SmbConnection.connect({
    server: "host",
    share: "sample-files",
    username: "smb",
    password: "password",
    domain: "host"
  });

  console.log(connection.connected);
} finally {
  if (connection?.connected) {
    await connection.disconnect();
  }
}
```

## Path Rules

- Paths are relative to the share root.
- `/` and `\` are both accepted.
- Leading slashes are removed internally.
- Use `""` to refer to the share root.

Examples:

```ts
await connection.stat("folder1/report.pdf");
await connection.stat("/folder1/report.pdf");
await connection.stat("\\folder1\\report.pdf");
await connection.opendir("");
```

## Stat a File or Directory

`stat()` returns metadata with file size as `bigint`.

```ts
const info = await connection.stat("folder1/fake_medical (1).c11.pdf");

console.log(info.name);
console.log(info.path);
console.log(info.size); // bigint
console.log(info.isDirectory);
console.log(info.modifiedAt);
```

## List a Directory

`opendir()` returns a directory handle. Call `readdir()` until it returns
`null`, then close the handle with `closedir()`.

```ts
const handle = await connection.opendir("folder1");

try {
  for (;;) {
    const entry = await connection.readdir(handle);
    if (entry === null) {
      break;
    }

    console.log(entry.name, entry.isDirectory, entry.size);
  }
} finally {
  await connection.closedir(handle);
}
```

Some servers may return `.` and `..` as directory entries. Filter them out if
you do not want them:

```ts
if (entry.name === "." || entry.name === "..") {
  continue;
}
```

## Read a File

1. Open the file.
2. Read from a `bigint` offset.
3. Close the handle.

```ts
const handle = await connection.open("folder1/fake_medical (1).c11.pdf", O_RDONLY);

try {
  const maxChunk = connection.getMaxReadSize();
  const chunk = await connection.read(handle, 0n, Math.min(maxChunk, 64 * 1024));

  console.log(chunk.length);
} finally {
  await connection.close(handle);
}
```

## Write a File

Use `O_CREAT | O_RDWR | O_TRUNC` to create or replace a file.

```ts
const handle = await connection.open(
  "folder1/output.bin",
  O_CREAT | O_RDWR | O_TRUNC
);

try {
  const payload = Buffer.from("hello over smb", "utf8");
  const written = await connection.write(handle, payload, 0n);

  console.log(written);
} finally {
  await connection.close(handle);
}
```

## Random Access and `bigint` Offsets

Offsets and sizes are `bigint`, which matters for large files and encrypted
data formats where block positions must stay exact.

```ts
const handle = await connection.open("folder1/encrypted.bin", O_CREAT | O_RDWR);

try {
  const blockOffset = 4_294_967_296n; // 4 GiB
  const block = new Uint8Array(4096);

  await connection.write(handle, block, blockOffset);
  const readBack = await connection.read(handle, blockOffset, block.length);

  console.log(readBack.length);
} finally {
  await connection.close(handle);
}
```

`read()` length is still a normal `number`, because Node buffers are sized with
numbers.

## Resize a File

```ts
const handle = await connection.open("folder1/output.bin", O_RDWR);

try {
  await connection.ftruncate(handle, 8_589_934_592n); // 8 GiB
} finally {
  await connection.close(handle);
}
```

## Rename and Delete

```ts
await connection.rename("folder1/output.bin", "folder1/output-renamed.bin");
await connection.unlink("folder1/output-renamed.bin");
```

For directories:

```ts
await connection.mkdir("folder1/new-dir");
await connection.rmdir("folder1/new-dir");
```

## Error Handling

The package maps native errors to typed JS errors.

```ts
import {
  SmbAuthenticationError,
  SmbNotFoundError,
  SmbTimeoutError
} from "node-smbx";

try {
  await connection.stat("folder1/missing.pdf");
} catch (error) {
  if (error instanceof SmbNotFoundError) {
    console.error("File does not exist");
  } else if (error instanceof SmbAuthenticationError) {
    console.error("Bad credentials");
  } else if (error instanceof SmbTimeoutError) {
    console.error("SMB request timed out");
  } else {
    throw error;
  }
}
```

## Practical Pattern

This pattern is a good default for most callers:

```ts
import {
  O_RDONLY,
  SmbConnection,
  SmbNotFoundError
} from "node-smbx";

let connection: SmbConnection | undefined;
let fileHandle: number | undefined;

try {
  connection = await SmbConnection.connect({
    server: "host",
    share: "sample-files",
    username: "smb",
    password: "password",
    domain: "host"
  });

  fileHandle = await connection.open("folder1/fake_medical (1).c11.pdf", O_RDONLY);
  const firstChunk = await connection.read(fileHandle, 0n, 4096);

  console.log(firstChunk.length);
} catch (error) {
  if (error instanceof SmbNotFoundError) {
    console.error("Missing file");
  } else {
    throw error;
  }
} finally {
  if (connection && fileHandle !== undefined) {
    await connection.close(fileHandle).catch(() => {});
  }
  if (connection?.connected) {
    await connection.disconnect().catch(() => {});
  }
}
```

## Notes

- `getMaxReadSize()` and `getMaxWriteSize()` report the current server limits.
- The package treats file contents as raw bytes. It does not encrypt or decrypt
  application data for you.
- SMB transport encryption is separate from application-level encryption.
