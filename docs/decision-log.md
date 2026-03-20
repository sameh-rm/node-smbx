# Decision Log

## 2026-03-20

- Reduced scope from a provider/stream package to a low-level SMB binding.
- Locked `bigint` for file sizes and offsets.
- Deferred Kerberos and SMB transport encryption from MVP.
- Chose a TypeScript wrapper over a Node-API native addon using `libsmb2`.
