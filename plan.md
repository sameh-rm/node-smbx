# node-smbx — Implementation Plan

Low-level Node-API native binding wrapping libsmb2. Exposes SMB2/3 primitives as a class-based async API.

---

## Decisions

| # | Decision | Choice |
|---|----------|--------|
| 1 | SMB library | Wrap libsmb2, vendor source, static link |
| 2 | LGPL compliance | Ship source + binding.gyp + LINKING.md. Satisfies LGPL 2.1 §6(a). |
| 3 | Platforms | All. Priority: Windows, Linux. Also: macOS. |
| 4 | I/O model | uv_poll (event loop native). No thread pool. |
| 5 | Auth | NTLMv2 always. Kerberos platform-conditional (krb5/GSS at build time). |
| 6 | Prebuilds | prebuildify from M1. node-gyp-build fallback. |
| 7 | Encryption | Deferred post-M4 |
| 8 | NAPI / engines | NAPI 9 → `engines.node: ">=18.17.0"` |
| 9 | API shape | Class-based (SmbConnection via Napi::ObjectWrap) |
| 10 | Errors | Typed error classes (NT status → JS error mapping) |
| 11 | Scope | Low-level binding only. No provider/pool/streams/generators. |
| 12 | Cancellation | No AbortSignal. Discard-result for single ops, context-destroy for full cancel. |
| 13 | Signing | smb2_set_security_mode() pre-connect, expose signingActive getter post-connect. |
| 14 | Timeouts | smb2_set_timeout(ctx, seconds). libsmb2 handles PDU timeouts. |
| 15 | Chunk clamping | Expose getMaxReadSize()/getMaxWriteSize(). Consumer clamps. |
| 16 | uv_poll multi-fd | Multiple uv_poll_t during connect (happy eyeballs), single after. |

---

## Phase 0: Scaffold & Packaging Foundation

**Goal:** Repo root is installable npm package. Empty addon loads. Consumer smoke test passes.

- [ ] 0.1 `.gitignore` — node_modules/, dist/, build/, prebuilds/
- [ ] 0.2 `LICENSE` — MIT (wrapper code)
- [ ] 0.3 `LGPL-2.1-LICENSE` — libsmb2 license text
- [ ] 0.4 `LINKING.md` — LGPL re-linking instructions
- [ ] 0.5 `package.json` — name, version, main, types, exports, engines, files, scripts, dependencies
- [ ] 0.6 `tsconfig.json` — ES2022, NodeNext, outDir dist/, declaration, strict
- [ ] 0.7 `binding.gyp` — initially just native/addon.cc (no libsmb2 yet)
- [ ] 0.8 Vendor libsmb2 source → `deps/libsmb2/` (include/ + lib/)
- [ ] 0.9 `native/addon.cc` — minimal module, loads successfully
- [ ] 0.10 `src/types.ts` — ConnectOptions, StatResult, DirEntry, FileHandle, DirHandle, open flags
- [ ] 0.11 `src/errors.ts` — 9 error classes + mapNtStatus()
- [ ] 0.12 `src/index.ts` — re-export types, errors, stub SmbConnection
- [ ] 0.13 Build: `npm run build` + `npm run build:native` succeed
- [ ] 0.14 `test/fixtures/consumer-app/` — package.json + test script
- [ ] 0.15 Smoke test: consumer-app installs, imports, addon loads

**Verification:**

- [ ] `npm run build` succeeds → dist/ with JS + .d.ts
- [ ] `npm run build:native` succeeds → build/Release/smbx.node
- [ ] Consumer-app `npm install && node test.js` passes
- [ ] `npm pack` produces tarball with correct contents
- [ ] Tarball install in temp dir works

---

## Phase 1 (M1): Connect + Auth + Signing + Stat

**Goal:** Connect to SMB server, authenticate, enforce signing, stat files.

### 1A: Native Connection Core

- [ ] 1A.1 `native/smb_connection.h` — class declaration (smb2_context*, uv_poll_t*, members)
- [ ] 1A.2 `native/smb_connection.cc` — Init, constructor, Connect (multi-fd poll), Disconnect
- [ ] 1A.3 uv_poll lifecycle: StartPoll, OnPoll (smb2_service_fd + re-arm), StopPoll
- [ ] 1A.4 Connect happy eyeballs: smb2_get_fds → multiple uv_poll_t + uv_timer_t → converge to single fd
- [ ] 1A.5 Connect callback: read max_read/write_size, server_security_mode, set signing_active_
- [ ] 1A.6 Signing enforcement: smb2_set_security_mode() before connect, reject if required but not active
- [ ] 1A.7 Implement Stat(path) → smb2_stat_async → {size BigInt, isDirectory, isSymlink, timestamps}
- [ ] 1A.8 Register all methods on JS class, update addon.cc

### 1B: TypeScript Layer *(parallel with 1A)*

- [ ] 1B.1 `src/native.ts` — node-gyp-build loader + typed native interface
- [ ] 1B.2 `src/errors.ts` — mapNtStatus() implementation (STATUS_LOGON_FAILURE → SmbAuthError, etc.)
- [ ] 1B.3 `src/index.ts` — real SmbConnection wrapper: static connect(), stat(), property getters

### 1C: Integration *(depends on 1A + 1B)*

- [ ] 1C.1 Wire TypeScript wrapper to native addon
- [ ] 1C.2 Unit tests: error mapping, type validation
- [ ] 1C.3 Integration tests: connect, stat, disconnect (require SMB server)
- [ ] 1C.4 Signing tests: required+supported → true, required+unsupported → SmbSigningRequiredError
- [ ] 1C.5 Update consumer smoke test

**Verification:**

- [ ] SmbConnection.connect() succeeds against test SMB server
- [ ] stat() returns correct metadata (size, isDirectory, timestamps)
- [ ] signingActive reflects actual signing state
- [ ] Errors are properly typed (SmbAuthenticationError, SmbNotFoundError, etc.)

---

## Phase 2 (M2): File I/O + Directory Operations

**Goal:** All remaining ops: open, read, write, close, ftruncate, mkdir, rmdir, unlink, rename, opendir, readdir, closedir.

### 2A: File I/O Primitives *(parallel with 2B)*

- [ ] 2A.1 `native/handle_map.h` — HandleMap<T>: insert→ID, get→ptr, remove→ptr. Monotonic IDs.
- [ ] 2A.2 Open(path, flags) → smb2_open_async → insert fh → FileHandle ID
- [ ] 2A.3 Read(handle, offset, length) → smb2_pread_async → Buffer (resize to actual bytes)
- [ ] 2A.4 Write(handle, data, offset) → smb2_pwrite_async → bytes written
- [ ] 2A.5 Ftruncate(handle, length) → smb2_ftruncate_async
- [ ] 2A.6 Close(handle) → remove from map → smb2_close_async

### 2B: Directory Operations *(parallel with 2A)*

- [ ] 2B.1 Mkdir(path) → smb2_mkdir_async
- [ ] 2B.2 Rmdir(path) → smb2_rmdir_async
- [ ] 2B.3 Unlink(path) → smb2_unlink_async
- [ ] 2B.4 Rename(old, new) → smb2_rename_async
- [ ] 2B.5 Opendir(path) → smb2_opendir_async → DirHandle ID
- [ ] 2B.6 Readdir(dirHandle) → smb2_readdir (sync) → DirEntry | null
- [ ] 2B.7 Closedir(dirHandle) → smb2_closedir (sync), remove from map

### 2C: TypeScript + Tests *(depends on 2A + 2B)*

- [ ] 2C.1 Update src/index.ts — all new methods on SmbConnection
- [ ] 2C.2 Export open flag constants: O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, O_EXCL
- [ ] 2C.3 Integration: write → close → open → read → verify contents
- [ ] 2C.4 Integration: read/write at specific offsets
- [ ] 2C.5 Integration: ftruncate, verify size
- [ ] 2C.6 Integration: mkdir → stat → rmdir
- [ ] 2C.7 Integration: create → unlink → stat fails
- [ ] 2C.8 Integration: create → rename → old gone, new exists
- [ ] 2C.9 Integration: opendir → readdir loop → closedir → all entries returned
- [ ] 2C.10 Error tests: invalid handle, nonexistent path, already exists

**Verification:**

- [ ] All file I/O operations work correctly
- [ ] Handle map prevents use-after-close
- [ ] All directory operations work
- [ ] Error types correct for each failure mode

---

## Phase 3 (M3): Hardening, CI, Packaging Verification

**Goal:** Production-quality. CI, prebuilds, full tests, packaging verified.

### 3A: CI Pipeline

- [ ] 3A.1 `.github/workflows/ci.yml` — build+test matrix (Linux/Win/macOS × Node 18/20/22)
- [ ] 3A.2 `.github/workflows/prebuild.yml` — prebuildify per platform, upload artifacts
- [ ] 3A.3 `.github/workflows/release.yml` — on tag: collect prebuilds → npm publish
- [ ] 3A.4 Docker samba container for integration tests (Linux CI)

### 3B: Test Hardening *(parallel with 3A)*

- [ ] 3B.1 Complete unit tests: error mappings, handle map, flag mapping, types
- [ ] 3B.2 Complete integration tests: full operation matrix
- [ ] 3B.3 Stress test: 1000 stats, no leaks
- [ ] 3B.4 Concurrent test: multiple SmbConnection on same event loop
- [ ] 3B.5 Edge cases: zero-byte read/write, empty dir, deep path, special chars
- [ ] 3B.6 Disconnect during operation: promises reject cleanly

### 3C: Packaging Verification *(parallel with 3A)*

- [ ] 3C.1 npm pack → extract → verify files present
- [ ] 3C.2 Tarball install in temp consumer → require works
- [ ] 3C.3 Local path install from consumer → works
- [ ] 3C.4 Prebuilt loads without node-gyp
- [ ] 3C.5 Fallback: delete prebuilds → node-gyp rebuild works
- [ ] 3C.6 LGPL artifacts present: deps/libsmb2/, LGPL-2.1-LICENSE, LINKING.md, binding.gyp, native/

### 3D: Documentation

- [ ] 3D.1 `README.md` — install, usage, Node req, build prereqs, API, signing, Kerberos, LGPL
- [ ] 3D.2 `docs/architecture.md` — uv_poll, handle map, diagram
- [ ] 3D.3 `docs/security.md` — signing enforcement, LGPL
- [ ] 3D.4 `docs/api.md` — full reference
- [ ] 3D.5 `docs/packaging.md` — prebuild, tarball, local install flows
- [ ] 3D.6 `docs/decision-log.md` — all decisions

**Verification:**

- [ ] CI green all platforms/nodes
- [ ] All tests pass
- [ ] Prebuilds for 5 platform/arch combos
- [ ] Tarball + local install both work
- [ ] LGPL compliance verified

---

## Phase 4 (M4): Benchmarks and Polish

**Goal:** Performance characterization, final polish.

- [ ] 4.1 `bench/stat-throughput.ts` — ops/sec
- [ ] 4.2 `bench/read-throughput.ts` — MB/sec chunked reads
- [ ] 4.3 `bench/write-throughput.ts` — MB/sec chunked writes
- [ ] 4.4 `bench/readdir-throughput.ts` — entries/sec
- [ ] 4.5 `bench/concurrent-connections.ts` — throughput with N connections
- [ ] 4.6 `bench/memory-usage.ts` — RSS tracking
- [ ] 4.7 Benchmark runner + structured output
- [ ] 4.8 Final README polish
- [ ] 4.9 `npm publish --dry-run` clean

**Verification:**

- [ ] Benchmarks produce reproducible results
- [ ] npm publish --dry-run clean
- [ ] Package ready for npm

---

## Key Design References

### Install / Build Lifecycle

| Hook | Triggers | Action |
|------|----------|--------|
| `prepare` | `npm install ../path`, dev checkout | `tsc && node-gyp rebuild` |
| `prepack` | `npm pack`, `npm publish` | `tsc` (prebuilds already exist from CI) |
| `install` | every consumer install | `node-gyp-build` (prebuild → fallback to node-gyp) |

### Tarball Contents (files whitelist)

`dist/`, `prebuilds/`, `native/`, `deps/libsmb2/`, `binding.gyp`, `LGPL-2.1-LICENSE`, `LINKING.md`, `README.md`

### Cancellation Rules

| Scenario | Behavior |
|----------|----------|
| Single op in flight | Cannot cancel. Runs to completion. Consumer can race with timeout. |
| Open file/dir handle | Consumer calls close()/closedir(). Cleaned on disconnect(). |
| Full connection | disconnect() → pending ops reject → poll stopped |
| Process exit | C++ destructor: sync disconnect + destroy context |

### Signing Enforcement

1. Pre-connect: `smb2_set_security_mode(ctx, ENABLED \| REQUIRED)`
2. libsmb2 validates during negotiate (fails if server can't sign)
3. Post-connect: store server_security_mode → expose `signingActive` getter
4. If `"required"` + connect succeeded → `signingActive = true` (guaranteed by libsmb2)

### uv_poll (Connect vs Steady-State)

**Connect**: smb2_get_fds() → N uv_poll_t + uv_timer_t → smb2_service_fd() → re-check fds → converge to 1
**Steady**: smb2_get_fd() → 1 uv_poll_t → smb2_service() → re-arm from smb2_which_events()

### Public API Summary

```typescript
class SmbConnection {
  static connect(opts: ConnectOptions): Promise<SmbConnection>
  disconnect(): Promise<void>
  stat(path): Promise<StatResult>
  open(path, flags): Promise<FileHandle>
  read(handle, offset, length): Promise<Buffer>
  write(handle, data, offset): Promise<number>
  ftruncate(handle, length): Promise<void>
  close(handle): Promise<void>
  mkdir(path): Promise<void>
  rmdir(path): Promise<void>
  unlink(path): Promise<void>
  rename(old, new): Promise<void>
  opendir(path): Promise<DirHandle>
  readdir(handle): Promise<DirEntry | null>
  closedir(handle): void
  getMaxReadSize(): number
  getMaxWriteSize(): number
  readonly signingActive: boolean
  readonly connected: boolean
}
