import { rm } from "node:fs/promises";

for (const path of ["dist", "build", "prebuilds"]) {
  await rm(path, { force: true, recursive: true });
}
