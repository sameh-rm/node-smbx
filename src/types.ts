export type ConnectOptions = {
  server: string;
  share: string;
  username: string;
  password: string;
  domain?: string;
  port?: number;
  signing?: "required" | "auto";
  timeoutSec?: number;
};

export type FileHandle = number;
export type DirHandle = number;

export type StatResult = {
  path: string;
  name: string;
  size: bigint;
  isDirectory: boolean;
  isSymlink: boolean;
  createdAt?: Date;
  modifiedAt?: Date;
  accessedAt?: Date;
};

export type DirEntry = StatResult;

export type NativeErrorLike = Error & {
  code?: string;
  errno?: number;
  nterror?: number;
  operation?: string;
  durationMs?: number;
  activeOperations?: Array<{
    action: string;
    path?: string;
    newPath?: string;
    handle?: number;
    durationMs: number;
    summary: string;
  }>;
  path?: string;
};

export type PendingOperationDebug = {
  action: string;
  path?: string;
  newPath?: string;
  handle?: number;
  durationMs: number;
  summary: string;
};

export type ConnectionDebugState = {
  connected: boolean;
  disconnecting: boolean;
  timeoutSec: number;
  pendingOperations: PendingOperationDebug[];
};

export type NativeSmbConnection = {
  disconnect(): Promise<void>;
  stat(path: string): Promise<StatResult>;
  open(path: string, flags: number): Promise<FileHandle>;
  read(handle: FileHandle, offset: bigint, length: number): Promise<Buffer>;
  write(handle: FileHandle, data: Buffer | Uint8Array, offset: bigint): Promise<number>;
  ftruncate(handle: FileHandle, length: bigint): Promise<void>;
  close(handle: FileHandle): Promise<void>;
  mkdir(path: string): Promise<void>;
  rmdir(path: string): Promise<void>;
  unlink(path: string): Promise<void>;
  rename(oldPath: string, newPath: string): Promise<void>;
  opendir(path: string): Promise<DirHandle>;
  readdir(handle: DirHandle): Promise<DirEntry | null>;
  closedir(handle: DirHandle): Promise<void>;
  getDebugState(): ConnectionDebugState;
  getMaxReadSize(): number;
  getMaxWriteSize(): number;
  readonly connected: boolean;
};

export type NativeBinding = {
  SmbConnection: {
    connect(options: ConnectOptions): Promise<NativeSmbConnection>;
  };
  constants: {
    O_RDONLY: number;
    O_WRONLY: number;
    O_RDWR: number;
    O_CREAT: number;
    O_EXCL: number;
    O_TRUNC: number;
  };
};
