import test from "node:test";
import assert from "node:assert/strict";
import { randomBytes, randomUUID } from "node:crypto";

import {
  O_CREAT,
  O_RDONLY,
  O_RDWR,
  O_TRUNC,
  SmbConnection,
  SmbInvalidStateError
} from "../../dist/index.js";

const server = process.env.SMB_SERVER || process.env.SMBX_SERVER;
const share = process.env.SMB_SHARE || process.env.SMBX_SHARE || "sample-files";
const username = process.env.SMB_USERNAME || process.env.SMBX_USERNAME;
const password = process.env.SMB_PASSWORD || process.env.SMBX_PASSWORD;
const domain = process.env.SMB_DOMAIN || process.env.SMBX_DOMAIN;

const credentials = {
  server,
  share,
  username,
  password,
  ...(domain ? { domain } : {})
};

const hasCredentials = Boolean(server && share && username && password);
const readOnly = process.env.SMB_READ_ONLY === "1";
const sharedReadPath = process.env.SMB_READ_PATH || "/";

async function connect() {
  return SmbConnection.connect(credentials);
}

function isSpecialDirectoryEntry(entry) {
  return entry?.name === "." || entry?.name === "..";
}

function dirname(targetPath) {
  const normalized = targetPath.replace(/\//g, "\\");
  const lastSeparator = normalized.lastIndexOf("\\");
  if (lastSeparator <= 0) {
    return "/";
  }
  return normalized.slice(0, lastSeparator);
}

async function findFirstReadableFile(connection, rootPath) {
  const queue = [rootPath];

  while (queue.length > 0) {
    const currentPath = queue.shift();
    if (!currentPath) {
      continue;
    }

    const stat = await connection.stat(currentPath);
    if (!stat.isDirectory) {
      return stat.path;
    }

    const handle = await connection.opendir(currentPath);
    try {
      for (;;) {
        const entry = await connection.readdir(handle);
        if (entry === null) {
          break;
        }
        if (isSpecialDirectoryEntry(entry)) {
          continue;
        }
        if (!entry.isDirectory) {
          return entry.path;
        }
        queue.push(entry.path);
      }
    } finally {
      await connection.closedir(handle).catch(() => {});
    }
  }

  return null;
}

async function readSampleFile(connection, targetPath, length) {
  const handle = await connection.open(targetPath, O_RDONLY);
  try {
    const readBack = await connection.read(handle, 0n, length);
    assert.ok(readBack instanceof Buffer);
    return readBack;
  } finally {
    await connection.close(handle).catch(() => {});
  }
}

test("integration: connect and disconnect", { skip: !hasCredentials }, async () => {
  const connection = await connect();

  assert.equal(connection.connected, true);
  await connection.disconnect();
  assert.equal(connection.connected, false);
});

test("integration: basic file and directory operations", { skip: !hasCredentials }, async () => {
  const connection = await connect();
  const rootDir = `node-smbx-${randomUUID()}`;
  const originalPath = `${rootDir}/cipher.bin`;
  const renamedPath = `${rootDir}/cipher-renamed.bin`;
  const payload = randomBytes(512);
  let fileHandle = null;
  let dirHandle = null;

  try {
    if (!readOnly) {
      await connection.mkdir(rootDir);
    }

    if (!readOnly) {
      const dirStat = await connection.stat(rootDir);
      assert.equal(dirStat.isDirectory, true);
      assert.equal(typeof dirStat.size, "bigint");
    } else {
      const dirStat = await connection.stat(sharedReadPath);
      assert.equal(typeof dirStat.size, "bigint");
    }

    if (!readOnly) {
      fileHandle = await connection.open(originalPath, O_CREAT | O_RDWR | O_TRUNC);
      const written = await connection.write(fileHandle, payload, 0n);
      assert.equal(written, payload.length);

      const readBack = await connection.read(fileHandle, 0n, payload.length);
      assert.deepEqual(readBack, payload);

      await connection.ftruncate(fileHandle, BigInt(payload.length + 128));
      await connection.close(fileHandle);
      fileHandle = null;

      const fileStat = await connection.stat(originalPath);
      assert.equal(fileStat.size, BigInt(payload.length + 128));

      await connection.rename(originalPath, renamedPath);
      const renamedStat = await connection.stat(renamedPath);
      assert.equal(renamedStat.size, BigInt(payload.length + 128));

      dirHandle = await connection.opendir(rootDir);
      const names = [];
      for (;;) {
        const entry = await connection.readdir(dirHandle);
        if (entry === null) {
          break;
        }
        names.push(entry.name);
      }
      assert.ok(names.includes("cipher-renamed.bin"));

      await connection.closedir(dirHandle);
      dirHandle = null;
    } else {
      const readableFilePath = await findFirstReadableFile(connection, sharedReadPath);
      assert.ok(readableFilePath, "expected at least one readable file on the share");
      await readSampleFile(connection, readableFilePath, payload.length);
    }

    await assert.rejects(connection.read(999999, 0n, 1), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });

    if (!readOnly) {
      await connection.unlink(renamedPath);
      await connection.rmdir(rootDir);
    }

    await connection.disconnect();
    assert.equal(connection.connected, false);
  } finally {
    if (fileHandle !== null) {
      await connection.close(fileHandle).catch(() => {});
    }
    if (dirHandle !== null) {
      await connection.closedir(dirHandle).catch(() => {});
    }
    if (!readOnly) {
      await connection.unlink(renamedPath).catch(() => {});
      await connection.unlink(originalPath).catch(() => {});
      await connection.rmdir(rootDir).catch(() => {});
    }
    if (connection.connected) {
      await connection.disconnect().catch(() => {});
    }
  }
});

test("integration: operations reject when disconnecting or disconnected", { skip: !hasCredentials }, async () => {
  const connection = await connect();
  const rootDir = `node-smbx-${randomUUID()}`;
  const originalPath = `${rootDir}/state.txt`;
  const renamedPath = `${rootDir}/state-renamed.txt`;
  let fileHandle = null;
  let dirHandle = null;
  let postDisconnectPath = originalPath;

  try {
    if (!readOnly) {
      await connection.mkdir(rootDir);
      fileHandle = await connection.open(originalPath, O_CREAT | O_RDWR | O_TRUNC);
      dirHandle = await connection.opendir(rootDir);
    } else {
      postDisconnectPath = await findFirstReadableFile(connection, sharedReadPath);
      assert.ok(postDisconnectPath, "expected at least one readable file on the share");
      fileHandle = await connection.open(postDisconnectPath, O_RDONLY);
      dirHandle = await connection.opendir(dirname(postDisconnectPath));
    }

    const disconnectPromise = connection.disconnect();

    await assert.rejects(connection.read(fileHandle, 0n, 1), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });

    await assert.rejects(connection.write(fileHandle, new Uint8Array([1]), 0n), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });

    await assert.rejects(connection.ftruncate(fileHandle, 0n), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });

    await assert.rejects(connection.close(fileHandle), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });

    await assert.rejects(connection.stat(postDisconnectPath), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });

    await assert.rejects(connection.readdir(dirHandle), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });

    await assert.rejects(connection.closedir(dirHandle), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });

    if (!readOnly) {
      await assert.rejects(connection.rename(originalPath, renamedPath), (error) => {
        assert.ok(error instanceof SmbInvalidStateError);
        return true;
      });

      await assert.rejects(connection.mkdir(`${rootDir}/nested`), (error) => {
        assert.ok(error instanceof SmbInvalidStateError);
        return true;
      });
    } else {
      await assert.rejects(connection.open(postDisconnectPath, O_RDONLY), (error) => {
        assert.ok(error instanceof SmbInvalidStateError);
        return true;
      });

      await assert.rejects(connection.opendir(dirname(postDisconnectPath)), (error) => {
        assert.ok(error instanceof SmbInvalidStateError);
        return true;
      });
    }

    await disconnectPromise;
    assert.equal(connection.connected, false);

    await assert.rejects(connection.stat(postDisconnectPath), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });

    if (!readOnly) {
      await assert.rejects(connection.unlink(originalPath), (error) => {
        assert.ok(error instanceof SmbInvalidStateError);
        return true;
      });
    }
  } finally {
    if (connection.connected) {
      await connection.disconnect().catch(() => {});
    }
    if (!readOnly) {
      const cleanup = await connect().catch(() => null);
      if (cleanup) {
        await cleanup.unlink(renamedPath).catch(() => {});
        await cleanup.unlink(originalPath).catch(() => {});
        await cleanup.rmdir(`${rootDir}/nested`).catch(() => {});
        await cleanup.rmdir(rootDir).catch(() => {});
        await cleanup.disconnect().catch(() => {});
      }
    }
  }
});

test("integration: repeated read-only connect and read cycles stay stable", { skip: !hasCredentials || !readOnly }, async () => {
  for (let attempt = 0; attempt < 5; attempt += 1) {
    const connection = await connect();
    try {
      const readableFilePath = await findFirstReadableFile(connection, sharedReadPath);
      assert.ok(readableFilePath, "expected at least one readable file on the share");

      const stat = await connection.stat(sharedReadPath);
      assert.equal(typeof stat.size, "bigint");

      await readSampleFile(connection, readableFilePath, 4096);
      await connection.disconnect();
      assert.equal(connection.connected, false);
    } finally {
      if (connection.connected) {
        await connection.disconnect().catch(() => {});
      }
    }
  }
});
