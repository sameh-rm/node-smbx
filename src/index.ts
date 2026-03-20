import { mapNativeError } from "./errors.js";
import { nativeBinding } from "./native.js";
import { normalizeSmbPath } from "./path.js";

import type {
  ConnectOptions,
  DirEntry,
  DirHandle,
  FileHandle,
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

function validateLength(length: number): void {
  if (!Number.isInteger(length) || length < 0) {
    throw new RangeError("length must be a non-negative integer");
  }
}

function validateBigInt(name: string, value: bigint): void {
  if (value < 0n) {
    throw new RangeError(`${name} must be non-negative`);
  }
}

export class SmbConnection {
  #native: NativeSmbConnection;

  private constructor(nativeConnection: NativeSmbConnection) {
    this.#native = nativeConnection;
  }

  static async connect(options: ConnectOptions): Promise<SmbConnection> {
    try {
      const nativeConnection = await nativeBinding.SmbConnection.connect(options);
      return new SmbConnection(nativeConnection);
    } catch (error) {
      throw mapNativeError(error);
    }
  }

  get connected(): boolean {
    return this.#native.connected;
  }

  disconnect(): Promise<void> {
    return this.#native.disconnect().catch((error) => {
      throw mapNativeError(error);
    });
  }

  stat(path: string): Promise<StatResult> {
    const normalized = normalizeSmbPath(path);
    return this.#native.stat(normalized).catch((error) => {
      throw mapNativeError(error, normalized);
    });
  }

  open(path: string, flags: number): Promise<FileHandle> {
    const normalized = normalizeSmbPath(path);
    return this.#native.open(normalized, flags).catch((error) => {
      throw mapNativeError(error, normalized);
    });
  }

  read(handle: FileHandle, offset: bigint, length: number): Promise<Buffer> {
    validateBigInt("offset", offset);
    validateLength(length);
    return this.#native.read(handle, offset, length).catch((error) => {
      throw mapNativeError(error);
    });
  }

  write(handle: FileHandle, data: Buffer | Uint8Array, offset: bigint): Promise<number> {
    validateBigInt("offset", offset);
    return this.#native.write(handle, data, offset).catch((error) => {
      throw mapNativeError(error);
    });
  }

  ftruncate(handle: FileHandle, length: bigint): Promise<void> {
    validateBigInt("length", length);
    return this.#native.ftruncate(handle, length).catch((error) => {
      throw mapNativeError(error);
    });
  }

  close(handle: FileHandle): Promise<void> {
    return this.#native.close(handle).catch((error) => {
      throw mapNativeError(error);
    });
  }

  mkdir(path: string): Promise<void> {
    const normalized = normalizeSmbPath(path);
    return this.#native.mkdir(normalized).catch((error) => {
      throw mapNativeError(error, normalized);
    });
  }

  rmdir(path: string): Promise<void> {
    const normalized = normalizeSmbPath(path);
    return this.#native.rmdir(normalized).catch((error) => {
      throw mapNativeError(error, normalized);
    });
  }

  unlink(path: string): Promise<void> {
    const normalized = normalizeSmbPath(path);
    return this.#native.unlink(normalized).catch((error) => {
      throw mapNativeError(error, normalized);
    });
  }

  rename(oldPath: string, newPath: string): Promise<void> {
    const normalizedOld = normalizeSmbPath(oldPath);
    const normalizedNew = normalizeSmbPath(newPath);
    return this.#native.rename(normalizedOld, normalizedNew).catch((error) => {
      throw mapNativeError(error);
    });
  }

  opendir(path: string): Promise<DirHandle> {
    const normalized = normalizeSmbPath(path);
    return this.#native.opendir(normalized).catch((error) => {
      throw mapNativeError(error, normalized);
    });
  }

  readdir(handle: DirHandle): Promise<DirEntry | null> {
    return this.#native.readdir(handle).catch((error) => {
      throw mapNativeError(error);
    });
  }

  closedir(handle: DirHandle): Promise<void> {
    return this.#native.closedir(handle).catch((error) => {
      throw mapNativeError(error);
    });
  }

  getMaxReadSize(): number {
    return this.#native.getMaxReadSize();
  }

  getMaxWriteSize(): number {
    return this.#native.getMaxWriteSize();
  }
}
