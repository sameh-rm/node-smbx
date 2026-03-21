import test from "node:test";
import assert from "node:assert/strict";

import {
  DEFAULT_TIMEOUT_SEC,
  O_RDONLY,
  SmbConnection,
  SmbInvalidStateError
} from "../../dist/index.js";

function createFakeNative(overrides = {}) {
  const state = {};
  const native = {
    connected: true,
    disconnect() {
      return Promise.resolve();
    },
    stat(path) {
      state.lastStatPath = path;
      return Promise.resolve({
        path,
        name: path.split("\\").at(-1) ?? "",
        size: 0n,
        isDirectory: false,
        isSymlink: false
      });
    },
    open(path, flags) {
      state.lastOpen = { path, flags };
      return Promise.resolve(1);
    },
    read() {
      return Promise.resolve(Buffer.alloc(0));
    },
    write() {
      return Promise.resolve(0);
    },
    ftruncate() {
      return Promise.resolve();
    },
    close() {
      return Promise.resolve();
    },
    mkdir(path) {
      state.lastMkdirPath = path;
      return Promise.resolve();
    },
    rmdir(path) {
      state.lastRmdirPath = path;
      return Promise.resolve();
    },
    unlink(path) {
      state.lastUnlinkPath = path;
      return Promise.resolve();
    },
    rename(oldPath, newPath) {
      state.lastRename = { oldPath, newPath };
      return Promise.resolve();
    },
    opendir(path) {
      state.lastOpendirPath = path;
      return Promise.resolve(1);
    },
    readdir() {
      return Promise.resolve(null);
    },
    closedir() {
      return Promise.resolve();
    },
    getDebugState() {
      return {
        connected: true,
        disconnecting: false,
        timeoutSec: DEFAULT_TIMEOUT_SEC,
        pendingOperations: [
          {
            action: "read",
            path: "sample-files\\patient.txt",
            durationMs: 1234,
            summary: "read path=sample-files\\patient.txt ageMs=1234"
          }
        ]
      };
    },
    getMaxReadSize() {
      return 65536;
    },
    getMaxWriteSize() {
      return 65536;
    },
    ...overrides
  };

  return {
    connection: new SmbConnection(native),
    state
  };
}

test("module exports the low-level API surface", () => {
  assert.equal(typeof SmbConnection, "function");
  assert.equal(typeof O_RDONLY, "number");
  assert.equal(typeof DEFAULT_TIMEOUT_SEC, "number");
});

test("SmbConnection.connect validates timeoutSec on the JS boundary", async () => {
  await assert.rejects(
    SmbConnection.connect({
      server: "192.168.1.5",
      share: "sample-files",
      username: "smb",
      password: "password",
      timeoutSec: -1
    }),
    (error) => error instanceof SmbInvalidStateError
  );
});

test("SmbConnection normalizes share-relative paths before calling native methods", async () => {
  const { connection, state } = createFakeNative();

  await connection.stat("/alpha/beta");
  await connection.rename("/alpha/beta", "\\gamma\\delta");
  await connection.mkdir("/cipher/data");
  await connection.opendir("\\cipher\\data");

  assert.equal(state.lastStatPath, "alpha\\beta");
  assert.deepEqual(state.lastRename, {
    oldPath: "alpha\\beta",
    newPath: "gamma\\delta"
  });
  assert.equal(state.lastMkdirPath, "cipher\\data");
  assert.equal(state.lastOpendirPath, "cipher\\data");
});

test("SmbConnection exposes connection debug state", () => {
  const { connection } = createFakeNative();
  const state = connection.getDebugState();

  assert.equal(state.connected, true);
  assert.equal(state.disconnecting, false);
  assert.equal(state.timeoutSec, DEFAULT_TIMEOUT_SEC);
  assert.equal(state.pendingOperations[0]?.action, "read");
  assert.match(
    state.pendingOperations[0]?.summary ?? "",
    /sample-files\\patient\.txt/
  );
});

test("SmbConnection validates bigint offsets and lengths on the JS boundary", () => {
  const { connection } = createFakeNative();

  assert.throws(
    () => connection.read(1, -1n, 16),
    (error) => error instanceof SmbInvalidStateError
  );
  assert.throws(
    () => connection.ftruncate(1, -1n),
    (error) => error instanceof SmbInvalidStateError
  );
  assert.throws(
    () => connection.read(1, 0n, -1),
    (error) => error instanceof SmbInvalidStateError
  );
});

test("SmbConnection maps native invalid-state failures to typed JS errors", async () => {
  const { connection } = createFakeNative({
    read() {
      return Promise.reject({
        message: "Unknown file handle",
        code: "INVALID_STATE"
      });
    }
  });

  await assert.rejects(connection.read(999, 0n, 16), (error) => {
    assert.ok(error instanceof SmbInvalidStateError);
    assert.match(error.message, /Unknown file handle/);
    return true;
  });
});
