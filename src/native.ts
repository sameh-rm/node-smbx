import load from "node-gyp-build";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import type { NativeBinding } from "./types.js";

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, "..");

export const nativeBinding = load(root) as NativeBinding;
