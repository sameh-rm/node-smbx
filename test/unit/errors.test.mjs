import test from "node:test";
import assert from "node:assert/strict";

import {
  SmbAuthenticationError,
  SmbNotFoundError,
  mapNativeError
} from "../../dist/errors.js";

test("mapNativeError maps authentication errors", () => {
  const error = mapNativeError({ message: "bad credentials", code: "AUTH" });
  assert.ok(error instanceof SmbAuthenticationError);
});

test("mapNativeError maps not-found errors", () => {
  const error = mapNativeError({ message: "missing", code: "NOT_FOUND" });
  assert.ok(error instanceof SmbNotFoundError);
});
