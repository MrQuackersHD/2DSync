from __future__ import annotations

from datetime import datetime

from sqlalchemy import BigInteger, DateTime, ForeignKey, Integer, String, Text, UniqueConstraint
from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column


class Base(DeclarativeBase):
    pass


class Account(Base):
    __tablename__ = "accounts"
    id: Mapped[str] = mapped_column(String(32), primary_key=True)
    created_at: Mapped[datetime] = mapped_column(DateTime)
    recovery_hash: Mapped[str] = mapped_column(String(64))
    quota_bytes: Mapped[int] = mapped_column(BigInteger)


class Device(Base):
    __tablename__ = "devices"
    id: Mapped[str] = mapped_column(String(32), primary_key=True)
    account_id: Mapped[str] = mapped_column(ForeignKey("accounts.id"), index=True)
    name: Mapped[str] = mapped_column(String(32))
    console_id: Mapped[str] = mapped_column(String(64))
    token_hash: Mapped[str] = mapped_column(String(64), unique=True, index=True)
    created_at: Mapped[datetime] = mapped_column(DateTime)
    revoked_at: Mapped[datetime | None] = mapped_column(DateTime, nullable=True)


class PairingCode(Base):
    __tablename__ = "pairing_codes"
    id: Mapped[str] = mapped_column(String(32), primary_key=True)
    account_id: Mapped[str] = mapped_column(ForeignKey("accounts.id"), index=True)
    code_hash: Mapped[str] = mapped_column(String(64), unique=True, index=True)
    expires_at: Mapped[datetime] = mapped_column(DateTime)
    used_at: Mapped[datetime | None] = mapped_column(DateTime, nullable=True)
    created_by: Mapped[str] = mapped_column(ForeignKey("devices.id"))


class PairingRequest(Base):
    __tablename__ = "pairing_requests"
    id: Mapped[str] = mapped_column(String(32), primary_key=True)
    account_id: Mapped[str] = mapped_column(ForeignKey("accounts.id"), index=True)
    code_id: Mapped[str] = mapped_column(ForeignKey("pairing_codes.id"))
    console_name: Mapped[str] = mapped_column(String(32))
    console_id: Mapped[str] = mapped_column(String(64))
    poll_token_hash: Mapped[str] = mapped_column(String(64), unique=True)
    status: Mapped[str] = mapped_column(String(16), index=True)
    device_id: Mapped[str | None] = mapped_column(String(32), nullable=True)
    delivery_token: Mapped[str | None] = mapped_column(Text, nullable=True)
    created_at: Mapped[datetime] = mapped_column(DateTime)


class SaveVersion(Base):
    __tablename__ = "save_versions"
    __table_args__ = (UniqueConstraint("account_id", "title_id", "id"),)
    id: Mapped[str] = mapped_column(String(32), primary_key=True)
    account_id: Mapped[str] = mapped_column(ForeignKey("accounts.id"), index=True)
    title_id: Mapped[str] = mapped_column(String(16), index=True)
    byte_size: Mapped[int] = mapped_column(BigInteger)
    checksum: Mapped[str] = mapped_column(String(64))
    content_checksum: Mapped[str] = mapped_column(String(64))
    storage_name: Mapped[str] = mapped_column(String(80))
    device_id: Mapped[str] = mapped_column(ForeignKey("devices.id"))
    created_at: Mapped[datetime] = mapped_column(DateTime)


class UploadSession(Base):
    __tablename__ = "upload_sessions"
    id: Mapped[str] = mapped_column(String(32), primary_key=True)
    account_id: Mapped[str] = mapped_column(ForeignKey("accounts.id"), index=True)
    title_id: Mapped[str] = mapped_column(String(16))
    expected_size: Mapped[int] = mapped_column(BigInteger)
    expected_checksum: Mapped[str] = mapped_column(String(64))
    content_checksum: Mapped[str] = mapped_column(String(64))
    received: Mapped[int] = mapped_column(BigInteger, default=0)
    storage_name: Mapped[str] = mapped_column(String(80))
    device_id: Mapped[str] = mapped_column(ForeignKey("devices.id"))
    expires_at: Mapped[datetime] = mapped_column(DateTime)
    status: Mapped[str] = mapped_column(String(16), default="open")


class RateLimit(Base):
    __tablename__ = "rate_limits"
    key: Mapped[str] = mapped_column(String(128), primary_key=True)
    failures: Mapped[int] = mapped_column(Integer, default=0)
    window_start: Mapped[datetime] = mapped_column(DateTime)
    locked_until: Mapped[datetime | None] = mapped_column(DateTime, nullable=True)
