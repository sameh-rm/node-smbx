# Security

MVP security behavior:

- `signing: "required"` must fail connection setup if a signed SMB session
  cannot be established.
- The package does not implement application-level encryption.
- SMB transport encryption is out of scope for MVP.
