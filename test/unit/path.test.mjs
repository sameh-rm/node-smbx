import test from "node:test";
import assert from "node:assert/strict";

import { normalizeSmbPath } from "../../dist/path.js";

test("normalizeSmbPath normalizes separators and roots", () => {
  assert.equal(normalizeSmbPath("/alpha/beta"), "alpha\\beta");
  assert.equal(normalizeSmbPath("\\alpha\\\\beta"), "alpha\\beta");
  assert.equal(normalizeSmbPath("/"), "");
});
