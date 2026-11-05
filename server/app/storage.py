from __future__ import annotations

import os

from .security import sha256_file


def account_dir(storage_dir: str, account_id: str) -> str:
    path = os.path.join(storage_dir, "blobs", account_id)
    os.makedirs(path, exist_ok=True)
    return path


def temp_path(storage_dir: str, upload_id: str) -> str:
    return os.path.join(storage_dir, "tmp", f"{upload_id}.partial")


def final_path(storage_dir: str, account_id: str, storage_name: str) -> str:
    return os.path.join(account_dir(storage_dir, account_id), storage_name)


def atomic_replace(src: str, dst: str) -> None:
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    os.replace(src, dst)


def file_checksum(path: str) -> str:
    return sha256_file(path)


def used_bytes(storage_dir: str, account_id: str) -> int:
    root = os.path.join(storage_dir, "blobs", account_id)
    if not os.path.isdir(root):
        return 0
    total = 0
    for name in os.listdir(root):
        path = os.path.join(root, name)
        if os.path.isfile(path):
            total += os.path.getsize(path)
    return total
