import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { execFileSync } from "node:child_process";

const root = resolve(".");
const tempDir = await mkdtemp(join(tmpdir(), "node-smbx-local-"));

try {
  await writeFile(
    join(tempDir, "package.json"),
    JSON.stringify(
      {
        name: "node-smbx-local-smoke",
        private: true,
        type: "module",
        dependencies: {
          "node-smbx": `file:${root}`
        }
      },
      null,
      2
    )
  );

  await writeFile(
    join(tempDir, "index.mjs"),
    [
      'import { SmbConnection, O_RDONLY } from "node-smbx";',
      'if (typeof SmbConnection !== "function" || typeof O_RDONLY !== "number") {',
      '  throw new Error("node-smbx import smoke check failed");',
      "}",
      'console.log("ok");',
      ""
    ].join("\n")
  );

  execFileSync("cmd", ["/c", "npm.cmd", "install"], { cwd: tempDir, stdio: "inherit" });
  execFileSync("node", ["index.mjs"], { cwd: tempDir, stdio: "inherit" });
} finally {
  await rm(tempDir, { force: true, recursive: true });
}
