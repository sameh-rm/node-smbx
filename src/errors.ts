import type { NativeErrorLike } from "./types.js";

export class SmbError extends Error {
  code?: string;
  errno?: number;
  nterror?: number;
  operation?: string;
  durationMs?: number;
  activeOperations?: NativeErrorLike["activeOperations"];
  path?: string;

  constructor(message: string, options: NativeErrorLike = new Error(message)) {
    super(message);
    this.name = new.target.name;
    this.code = options.code;
    this.errno = options.errno;
    this.nterror = options.nterror;
    this.operation = options.operation;
    this.durationMs = options.durationMs;
    this.activeOperations = options.activeOperations;
    this.path = options.path;
    if ("cause" in options) {
      (this as Error & { cause?: unknown }).cause = options.cause;
    }
  }
}

export class SmbConnectionError extends SmbError {}
export class SmbAuthenticationError extends SmbError {}
export class SmbSigningRequiredError extends SmbError {}
export class SmbProtocolError extends SmbError {}
export class SmbTimeoutError extends SmbError {}
export class SmbNotFoundError extends SmbError {}
export class SmbAlreadyExistsError extends SmbError {}
export class SmbInvalidStateError extends SmbError {}

export function mapNativeError(error: unknown, path?: string): SmbError {
  const source = (error ?? new Error("Unknown SMB error")) as NativeErrorLike;
  source.path ??= path;

  switch (source.code) {
    case "AUTH":
      return new SmbAuthenticationError(source.message, source);
    case "SIGNING_REQUIRED":
      return new SmbSigningRequiredError(source.message, source);
    case "TIMEOUT":
      return new SmbTimeoutError(source.message, source);
    case "NOT_FOUND":
      return new SmbNotFoundError(source.message, source);
    case "ALREADY_EXISTS":
      return new SmbAlreadyExistsError(source.message, source);
    case "INVALID_STATE":
      return new SmbInvalidStateError(source.message, source);
    case "PROTOCOL":
      return new SmbProtocolError(source.message, source);
    case "CONNECTION":
    default:
      return new SmbConnectionError(source.message, source);
  }
}
