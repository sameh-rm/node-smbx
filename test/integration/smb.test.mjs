import test from "node:test";
import assert from "node:assert/strict";
import { randomBytes, randomUUID } from "node:crypto";

import {
  O_CREAT,
  O_RDWR,
  O_TRUNC,
  SmbConnection,
  SmbInvalidStateError
} from "../../dist/index.js";

const server = process.env.SMBX_SERVER;
const share = process.env.SMBX_SHARE;
const username = process.env.SMBX_USERNAME;
const password = process.env.SMBX_PASSWORD;
const domain = process.env.SMBX_DOMAIN;

const credentials = {
  server,
  share,
  username,
  password,
  ...(domain ? { domain } : {})
};

const hasCredentials = Boolean(server && share && username && password);

async function connect() {
  return SmbConnection.connect(credentials);
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
    await connection.mkdir(rootDir);

    const dirStat = await connection.stat(rootDir);
    assert.equal(dirStat.isDirectory, true);
    assert.equal(typeof dirStat.size, "bigint");

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

    await assert.rejects(connection.read(999999, 0n, 1), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });

    await connection.unlink(renamedPath);
    await connection.rmdir(rootDir);

    await connection.disconnect();
    assert.equal(connection.connected, false);
  } finally {
    if (fileHandle !== null) {
      await connection.close(fileHandle).catch(() => {});
    }
    if (dirHandle !== null) {
      await connection.closedir(dirHandle).catch(() => {});
    }
    await connection.unlink(renamedPath).catch(() => {});
    await connection.unlink(originalPath).catch(() => {});
    await connection.rmdir(rootDir).catch(() => {});
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

  try {
    await connection.mkdir(rootDir);
    fileHandle = await connection.open(originalPath, O_CREAT | O_RDWR | O_TRUNC);
    dirHandle = await connection.opendir(rootDir);

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

    await assert.rejects(connection.readdir(dirHandle), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });

    await assert.rejects(connection.closedir(dirHandle), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });

    await assert.rejects(connection.rename(originalPath, renamedPath), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });

    await assert.rejects(connection.mkdir(`${rootDir}/nested`), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });

    await disconnectPromise;
    assert.equal(connection.connected, false);

    await assert.rejects(connection.unlink(originalPath), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });
  } finally {
    if (connection.connected) {
      await connection.disconnect().catch(() => {});
    }
    const cleanup = await connect().catch(() => null);
    if (cleanup) {
      await cleanup.unlink(renamedPath).catch(() => {});
      await cleanup.unlink(originalPath).catch(() => {});
      await cleanup.rmdir(`${rootDir}/nested`).catch(() => {});
      await cleanup.rmdir(rootDir).catch(() => {});
      await cleanup.disconnect().catch(() => {});
    }
  }
});
