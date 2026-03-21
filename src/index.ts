import { mapNativeError, SmbError, SmbInvalidStateError } from "./errors.js";
import { nativeBinding } from "./native.js";
import { normalizeSmbPath } from "./path.js";

import type {
  ConnectOptions,
  DirEntry,
  DirHandle,
  FileHandle,
  NativeErrorLike,
  NativeSmbConnection,
  StatResult
} from "./types.js";

export * from "./errors.js";
export * from "./types.js";
export { normalizeSmbPath } from "./path.js";

export const O_RDONLY = nativeBinding.constants.O_RDONLY;
export const O_WRONLY = nativeBinding.constants.O_WRONLY;
export const O_RDWR = nativeBinding.constants.O_RDWR;
export const O_CREAT = nativeBinding.constants.O_CREAT;
export const O_EXCL = nativeBinding.constants.O_EXCL;
export const O_TRUNC = nativeBinding.constants.O_TRUNC;

function invalidStateError(message: string, path?: string): SmbInvalidStateError {
  const nativeLike: NativeErrorLike = Object.assign(new Error(message), {
    code: "INVALID_STATE",
    path
  });
  return new SmbInvalidStateError(message, nativeLike);
}

function toSmbError(error: unknown, path?: string): SmbError {
  if (error instanceof SmbError) {
    return error;
  }
  if (error instanceof RangeError || error instanceof TypeError) {
    return invalidStateError(error.message, path);
  }
  return mapNativeError(error, path);
}

function validateLength(length: number): void {
  if (!Number.isInteger(length) || length < 0) {
    throw invalidStateError("length must be a non-negative integer");
  }
}

function validateBigInt(name: string, value: bigint): void {
  if (value < 0n) {
    throw invalidStateError(`${name} must be non-negative`);
  }
}

function validatePathInput(name: string, value: unknown): string {
  if (typeof value !== "string") {
    throw invalidStateError(`${name} must be a string`);
  }
  return value;
}

function validateHandle(name: string, value: unknown): number {
  if (!Number.isInteger(value)) {
    throw invalidStateError(`${name} must be a valid handle`);
  }
  return value as number;
}

function validateData(data: Buffer | Uint8Array): void {
  if (!Buffer.isBuffer(data) && !(data instanceof Uint8Array)) {
    throw invalidStateError("data must be a Buffer or Uint8Array");
  }
}

export class SmbConnection {
  #native: NativeSmbConnection;

  private constructor(nativeConnection: NativeSmbConnection) {
    this.#native = nativeConnection;
  }

  static async connect(options: ConnectOptions): Promise<SmbConnection> {
    try {
      if (!options || typeof options !== "object") {
        throw invalidStateError("options must be an object");
      }
      if (typeof options.server !== "string" || options.server.trim() === "") {
        throw invalidStateError("server must be a non-empty string");
      }
      if (typeof options.share !== "string" || options.share.trim() === "") {
        throw invalidStateError("share must be a non-empty string");
      }
      if (typeof options.username !== "string" || options.username.trim() === "") {
        throw invalidStateError("username must be a non-empty string");
      }
      if (typeof options.password !== "string") {
        throw invalidStateError("password must be a string");
      }
      const nativeConnection = await nativeBinding.SmbConnection.connect(options);
      return new SmbConnection(nativeConnection);
    } catch (error) {
      throw toSmbError(error);
    }
  }

  get connected(): boolean {
    return this.#native.connected;
  }

  disconnect(): Promise<void> {
    return this.#native.disconnect().catch((error) => {
      throw toSmbError(error);
    });
  }

  stat(path: string): Promise<StatResult> {
    const normalized = normalizeSmbPath(validatePathInput("path", path));
    return this.#native.stat(normalized).catch((error) => {
      throw toSmbError(error, normalized);
    });
  }

  open(path: string, flags: number): Promise<FileHandle> {
    const normalized = normalizeSmbPath(validatePathInput("path", path));
    return this.#native.open(normalized, flags).catch((error) => {
      throw toSmbError(error, normalized);
    });
  }

  read(handle: FileHandle, offset: bigint, length: number): Promise<Buffer> {
    validateHandle("handle", handle);
    validateBigInt("offset", offset);
    validateLength(length);
    return this.#native.read(handle, offset, length).catch((error) => {
      throw toSmbError(error);
    });
  }

  write(handle: FileHandle, data: Buffer | Uint8Array, offset: bigint): Promise<number> {
    validateHandle("handle", handle);
    validateBigInt("offset", offset);
    validateData(data);
    return this.#native.write(handle, data, offset).catch((error) => {
      throw toSmbError(error);
    });
  }

  ftruncate(handle: FileHandle, length: bigint): Promise<void> {
    validateHandle("handle", handle);
    validateBigInt("length", length);
    return this.#native.ftruncate(handle, length).catch((error) => {
      throw toSmbError(error);
    });
  }

  close(handle: FileHandle): Promise<void> {
    validateHandle("handle", handle);
    return this.#native.close(handle).catch((error) => {
      throw toSmbError(error);
    });
  }

  mkdir(path: string): Promise<void> {
    const normalized = normalizeSmbPath(validatePathInput("path", path));
    return this.#native.mkdir(normalized).catch((error) => {
      throw toSmbError(error, normalized);
    });
  }

  rmdir(path: string): Promise<void> {
    const normalized = normalizeSmbPath(validatePathInput("path", path));
    return this.#native.rmdir(normalized).catch((error) => {
      throw toSmbError(error, normalized);
    });
  }

  unlink(path: string): Promise<void> {
    const normalized = normalizeSmbPath(validatePathInput("path", path));
    return this.#native.unlink(normalized).catch((error) => {
      throw toSmbError(error, normalized);
    });
  }

  rename(oldPath: string, newPath: string): Promise<void> {
    const normalizedOld = normalizeSmbPath(validatePathInput("oldPath", oldPath));
    const normalizedNew = normalizeSmbPath(validatePathInput("newPath", newPath));
    return this.#native.rename(normalizedOld, normalizedNew).catch((error) => {
      throw toSmbError(error);
    });
  }

  opendir(path: string): Promise<DirHandle> {
    const normalized = normalizeSmbPath(validatePathInput("path", path));
    return this.#native.opendir(normalized).catch((error) => {
      throw toSmbError(error, normalized);
    });
  }

  readdir(handle: DirHandle): Promise<DirEntry | null> {
    validateHandle("handle", handle);
    return this.#native.readdir(handle).catch((error) => {
      throw toSmbError(error);
    });
  }

  closedir(handle: DirHandle): Promise<void> {
    validateHandle("handle", handle);
    return this.#native.closedir(handle).catch((error) => {
      throw toSmbError(error);
    });
  }

  getMaxReadSize(): number {
    return this.#native.getMaxReadSize();
  }

  getMaxWriteSize(): number {
    return this.#native.getMaxWriteSize();
  }
}
