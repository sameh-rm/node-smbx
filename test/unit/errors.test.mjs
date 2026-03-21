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

test("mapNativeError preserves stall diagnostics", () => {
  const error = mapNativeError({
    message: "SMB request timed out [active=read path=sample-files\\\\patient.txt ageMs=30001]",
    code: "TIMEOUT",
    operation: "read",
    durationMs: 30001,
    activeOperations: [
      {
        action: "read",
        path: "sample-files\\patient.txt",
        durationMs: 30001,
        summary: "read path=sample-files\\patient.txt ageMs=30001"
      }
    ]
  });

  assert.equal(error.operation, "read");
  assert.equal(error.durationMs, 30001);
  assert.equal(error.activeOperations?.[0]?.action, "read");
  assert.match(error.message, /active=read/);
});
