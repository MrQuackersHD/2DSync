# Changelog

## 0.1.0

- Rebuilt the CIA so the system loader accepts it. The first file crashed on launch with an undefined instruction in the loader.
- Kept uninitialized memory out of the code file after a loader data abort, and set the HOME Menu name and banner to 2DSync.
- Moved the application state off the 32KB stack. That object is 88KB and was crashing on the first call.
- Stopped treating the system font as missing. Citro2d returns no font handle for the console's own region, and the app was exiting with the GPU still active.
- Game icons are copied as 8x8 tiles, touch targets match the drawn rows, and settings replace the existing SD file instead of failing on the second save. The default server is the public Tailscale name, shown in full.
- First 3DS homebrew client for listing supported titles, copying save archives, and keeping local backups.
- Pairing with short-lived single-use sync codes, per-console tokens, recovery codes, and device revocation.
- FastAPI server with checksum-verified chunked uploads, version history, quotas, and PostgreSQL or SQLite.
- Conflict screen that asks before a local save and a cloud save overwrite each other.
- Offline local backup and restore. Cloud actions stop when the console has no network.
