import { mkdtemp, readdir, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { execFileSync } from "node:child_process";

const root = resolve(".");
const tempDir = await mkdtemp(join(tmpdir(), "node-smbx-tarball-"));

try {
  execFileSync("cmd", ["/c", "npm.cmd", "pack"], { cwd: root, stdio: "inherit" });
  const tarballs = (await readdir(root)).filter((entry) => entry.endsWith(".tgz"));
  if (tarballs.length === 0) {
    throw new Error("npm pack did not produce a tarball");
  }

  const tarball = resolve(root, tarballs.sort().at(-1));

  await writeFile(
    join(tempDir, "package.json"),
    JSON.stringify(
      {
        name: "node-smbx-tarball-smoke",
        private: true,
        type: "module"
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
      '  throw new Error("node-smbx tarball smoke check failed");',
      "}",
      'console.log("ok");',
      ""
    ].join("\n")
  );

  execFileSync("cmd", ["/c", "npm.cmd", "install", tarball], { cwd: tempDir, stdio: "inherit" });
  execFileSync("node", ["index.mjs"], { cwd: tempDir, stdio: "inherit" });
} finally {
  await rm(tempDir, { force: true, recursive: true });
  const tarballs = (await readdir(root)).filter((entry) => entry.endsWith(".tgz"));
  await Promise.all(tarballs.map((entry) => rm(join(root, entry), { force: true })));
}
