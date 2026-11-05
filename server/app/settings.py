from __future__ import annotations

import os
from dataclasses import dataclass


def _int(name: str, default: int) -> int:
    raw = os.environ.get(name)
    if raw is None or raw == "":
        return default
    return int(raw)


@dataclass
class Settings:
    database_url: str
    storage_dir: str
    token_pepper: str
    pairing_ttl_seconds: int
    max_failed_attempts: int
    lockout_seconds: int
    quota_bytes: int
    max_save_bytes: int
    retention_versions: int
    chunk_size: int
    upload_ttl_seconds: int

    @classmethod
    def from_env(cls) -> "Settings":
        return cls(
            database_url=os.environ.get("DATABASE_URL", "sqlite:///./data/2dsync.db"),
            storage_dir=os.environ.get("SAVE_STORAGE_DIR", "./data/saves"),
            token_pepper=os.environ.get("TOKEN_PEPPER", ""),
            pairing_ttl_seconds=_int("PAIRING_TTL_SECONDS", 600),
            max_failed_attempts=_int("MAX_FAILED_ATTEMPTS", 5),
            lockout_seconds=_int("LOCKOUT_SECONDS", 900),
            quota_bytes=_int("QUOTA_BYTES", 256 * 1024 * 1024),
            max_save_bytes=_int("MAX_SAVE_BYTES", 32 * 1024 * 1024),
            retention_versions=_int("RETENTION_VERSIONS", 10),
            chunk_size=_int("CHUNK_SIZE", 32 * 1024),
            upload_ttl_seconds=_int("UPLOAD_TTL_SECONDS", 3600),
        )

    def ensure_dirs(self) -> None:
        os.makedirs(self.storage_dir, exist_ok=True)
        os.makedirs(os.path.join(self.storage_dir, "tmp"), exist_ok=True)
        os.makedirs(os.path.join(self.storage_dir, "blobs"), exist_ok=True)
        if not self.token_pepper:
            path = os.path.join(self.storage_dir, ".pepper")
            if os.path.exists(path):
                with open(path, "r", encoding="utf-8") as handle:
                    self.token_pepper = handle.read().strip()
            if not self.token_pepper:
                import secrets

                self.token_pepper = secrets.token_hex(32)
                with open(path, "w", encoding="utf-8") as handle:
                    handle.write(self.token_pepper)
                os.chmod(path, 0o600)
