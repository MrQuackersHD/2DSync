from __future__ import annotations

import hashlib
import hmac
import secrets
import string

ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"


def new_id() -> str:
    return secrets.token_hex(16)


def new_token() -> str:
    return secrets.token_urlsafe(32)


def new_code(length: int) -> str:
    return "".join(secrets.choice(ALPHABET) for _ in range(length))


def format_recovery(code: str) -> str:
    return f"{code[0:4]}-{code[4:8]}-{code[8:12]}"


def normalize_code(code: str) -> str:
    return "".join(ch for ch in code.upper() if ch in ALPHABET)


def hash_secret(pepper: str, secret: str) -> str:
    return hmac.new(pepper.encode("utf-8"), secret.encode("utf-8"), hashlib.sha256).hexdigest()


def secrets_equal(left: str, right: str) -> bool:
    return hmac.compare_digest(left, right)


def sha256_file(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()
