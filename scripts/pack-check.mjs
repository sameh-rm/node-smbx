import { access } from "node:fs/promises";

await access("package.json");
await access("README.md");
