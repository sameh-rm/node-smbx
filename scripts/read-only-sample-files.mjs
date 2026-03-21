import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";

import {
  O_RDONLY,
  SmbConnection,
  SmbInvalidStateError
} from "../dist/index.js";

const server = process.env.SMB_SERVER || process.env.SMBX_SERVER;
const share = process.env.SMB_SHARE || process.env.SMBX_SHARE || "sample-files";
const username = process.env.SMB_USERNAME || process.env.SMBX_USERNAME;
const password = process.env.SMB_PASSWORD || process.env.SMBX_PASSWORD;
const domain = process.env.SMB_DOMAIN || process.env.SMBX_DOMAIN;
const readPath = process.env.SMB_READ_PATH || "/";

const hasCredentials = Boolean(server && share && username && password);
if (!hasCredentials) {
  throw new Error(
    "Missing SMB credentials. Set SMB_SERVER, SMB_USERNAME, SMB_PASSWORD, SMB_SHARE"
  );
}

const credentials = {
  server,
  share,
  username,
  password,
  ...(domain ? { domain } : {})
};

async function connect() {
  return SmbConnection.connect(credentials);
}

function isSpecialDirectoryEntry(entry) {
  return entry?.name === "." || entry?.name === "..";
}

const readWholeFile = async (connection, targetPath) => {
  const handle = await connection.open(targetPath, O_RDONLY);
  const chunkSize = Math.max(connection.getMaxReadSize(), 64 * 1024);
  let bytesRead = 0;

  try {
    let offset = 0n;
    for (;;) {
      const chunk = await connection.read(handle, offset, chunkSize);
      if (chunk.length === 0) {
        break;
      }
      bytesRead += chunk.length;
      offset += BigInt(chunk.length);
      if (chunk.length < chunkSize) {
        break;
      }
    }
    return bytesRead;
  } finally {
    await connection.close(handle).catch(() => {});
  }
};

const walkAndRead = async (connection, root) => {
  const rootStat = await connection.stat(root);
  const queue = [{ path: root, stat: rootStat }];
  const result = {
    root,
    filesRead: 0,
    directoriesVisited: 0,
    totalBytesRead: 0,
    sampleFiles: []
  };

  while (queue.length > 0) {
    const current = queue.shift();
    if (!current) {
      continue;
    }

    if (!current.stat.isDirectory) {
      const bytesRead = await readWholeFile(connection, current.path);
      result.filesRead += 1;
      result.totalBytesRead += bytesRead;
      if (result.sampleFiles.length < 10) {
        result.sampleFiles.push({
          path: current.path,
          bytesRead
        });
      }
      continue;
    }

    result.directoriesVisited += 1;
    const handle = await connection.opendir(current.path);

    try {
      for (;;) {
        const entry = await connection.readdir(handle);
        if (entry === null) {
          break;
        }
        if (isSpecialDirectoryEntry(entry)) {
          continue;
        }
        queue.push({ path: entry.path, stat: entry });
      }
    } finally {
      await connection.closedir(handle).catch(() => {});
    }
  }

  return result;
};

const run = async () => {
  const connection = await connect();
  const rootDir = `node-smbx-ro-${randomUUID()}`;

  try {
    assert.equal(connection.connected, true);

    const rootStat = await connection.stat(readPath);
    assert.equal(typeof rootStat.size, "bigint");

    const readResult = await walkAndRead(connection, readPath);
    assert.ok(readResult.directoriesVisited > 0 || readResult.filesRead > 0);

    await assert.rejects(connection.read(999999, 0n, 1), (error) => {
      assert.ok(error instanceof SmbInvalidStateError);
      return true;
    });

    // Read-only sanity: make sure create fails (we don't assert error type).
    await assert.rejects(connection.mkdir(rootDir));

    await connection.disconnect();
    assert.equal(connection.connected, false);

    console.log(
      JSON.stringify(
        {
          root: readResult.root,
          filesRead: readResult.filesRead,
          directoriesVisited: readResult.directoriesVisited,
          totalBytesRead: readResult.totalBytesRead,
          sampleFiles: readResult.sampleFiles
        },
        null,
        2
      )
    );
  } finally {
    if (connection.connected) {
      await connection.disconnect().catch(() => {});
    }
  }
};

run().catch((error) => {
  console.error("read-only sample-files test failed:", error?.message || error);
  process.exit(1);
});
