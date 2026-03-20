# API

The public API is a low-level `SmbConnection` class with:

- connection lifecycle
- stat
- file handle operations
- directory handle operations
- max read/write size queries

All size and offset metadata is represented as `bigint`.

See `docs/usage.md` for end-to-end examples.
