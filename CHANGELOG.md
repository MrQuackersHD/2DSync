# Changelog

## 0.1.0

- Rebuilt the CIA so the system loader accepts it. The first file crashed on launch with an undefined instruction in the loader.
- First 3DS homebrew client for listing supported titles, copying save archives, and keeping local backups.
- Pairing with short-lived single-use sync codes, per-console tokens, recovery codes, and device revocation.
- FastAPI server with checksum-verified chunked uploads, version history, quotas, and PostgreSQL or SQLite.
- Conflict screen that asks before a local save and a cloud save overwrite each other.
- Offline local backup and restore. Cloud actions stop when the console has no network.
