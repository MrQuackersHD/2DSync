from __future__ import annotations

import logging
import os
import re
from datetime import datetime, timedelta, timezone

from fastapi import APIRouter, Depends, FastAPI, HTTPException, Request
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field
from sqlalchemy import select
from sqlalchemy.orm import Session

from .db import init_database, make_engine, make_session_factory
from .models import Account, Device, PairingCode, PairingRequest, RateLimit, SaveVersion, UploadSession
from .security import format_recovery, hash_secret, new_code, new_id, new_token, normalize_code
from .settings import Settings
from .storage import atomic_replace, file_checksum, final_path, temp_path

log = logging.getLogger("2dsync")
router = APIRouter()
TITLE_RE = re.compile(r"^[0-9A-Fa-f]{16}$")
NAME_RE = re.compile(r"^[\w .'\-]{1,32}$")


def utcnow() -> datetime:
    return datetime.now(timezone.utc).replace(tzinfo=None)


def iso(value: datetime) -> str:
    return value.isoformat() + "Z"


def db_session(request: Request):
    SessionLocal = request.app.state.Session
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def settings_of(request: Request) -> Settings:
    return request.app.state.settings


def client_ip(request: Request) -> str:
    return request.client.host if request.client else "unknown"


def clean_name(name: str) -> str:
    name = name.strip()
    if not NAME_RE.match(name):
        raise HTTPException(status_code=422, detail="Console name must be 1-32 letters, numbers, spaces, or .'-")
    return name


def clean_title(title_id: str) -> str:
    if not TITLE_RE.match(title_id):
        raise HTTPException(status_code=422, detail="Title ID must be 16 hex characters")
    return title_id.upper()


def check_rate(db: Session, key: str, settings: Settings) -> None:
    now = utcnow()
    row = db.get(RateLimit, key)
    if row and row.locked_until and row.locked_until > now:
        raise HTTPException(status_code=429, detail="Too many attempts. Try again later.")
    if row and (now - row.window_start).total_seconds() > settings.lockout_seconds:
        row.failures = 0
        row.window_start = now
        row.locked_until = None
        db.commit()


def record_failure(db: Session, key: str, settings: Settings) -> None:
    now = utcnow()
    row = db.get(RateLimit, key)
    if row is None:
        row = RateLimit(key=key, failures=0, window_start=now, locked_until=None)
        db.add(row)
    if (now - row.window_start).total_seconds() > settings.lockout_seconds:
        row.failures = 0
        row.window_start = now
        row.locked_until = None
    row.failures += 1
    if row.failures >= settings.max_failed_attempts:
        row.locked_until = now + timedelta(seconds=settings.lockout_seconds)
    db.commit()


def clear_failures(db: Session, key: str) -> None:
    row = db.get(RateLimit, key)
    if row:
        db.delete(row)
        db.commit()


def current_device(request: Request, db: Session = Depends(db_session)) -> Device:
    header = request.headers.get("authorization", "")
    if not header.lower().startswith("bearer "):
        raise HTTPException(status_code=401, detail="Missing device token")
    token = header[7:].strip()
    if not token:
        raise HTTPException(status_code=401, detail="Missing device token")
    digest = hash_secret(request.app.state.settings.token_pepper, token)
    device = db.scalar(select(Device).where(Device.token_hash == digest))
    if device is None or device.revoked_at is not None:
        raise HTTPException(status_code=401, detail="Device is not authorized")
    return device


def issue_device(db: Session, settings: Settings, account_id: str, name: str, console_id: str) -> tuple[Device, str]:
    token = new_token()
    device = Device(
        id=new_id(),
        account_id=account_id,
        name=name,
        console_id=console_id[:64],
        token_hash=hash_secret(settings.token_pepper, token),
        created_at=utcnow(),
        revoked_at=None,
    )
    db.add(device)
    return device, token


class CreateAccountIn(BaseModel):
    console_name: str
    console_id: str = Field(min_length=8, max_length=64)


class RedeemIn(BaseModel):
    code: str
    console_name: str
    console_id: str = Field(min_length=8, max_length=64)


class RecoveryIn(BaseModel):
    recovery_code: str
    console_name: str
    console_id: str = Field(min_length=8, max_length=64)


class UploadStartIn(BaseModel):
    byte_size: int = Field(gt=0)
    checksum_sha256: str = Field(min_length=64, max_length=64)
    content_sha256: str = Field(min_length=64, max_length=64)


def device_out(device: Device, token: str | None = None) -> dict:
    payload = {
        "device_id": device.id,
        "name": device.name,
        "console_id": device.console_id,
        "created_at": iso(device.created_at),
        "revoked": device.revoked_at is not None,
    }
    if token:
        payload["token"] = token
    return payload


@router.get("/health")
def health(request: Request, db: Session = Depends(db_session)):
    db.scalar(select(Account.id).limit(1))
    probe = os.path.join(request.app.state.settings.storage_dir, ".health")
    with open(probe, "w", encoding="utf-8") as handle:
        handle.write("ok")
    os.remove(probe)
    return {"status": "ok", "version": "0.1.0"}


@router.post("/v1/accounts", status_code=201)
def create_account(body: CreateAccountIn, request: Request, db: Session = Depends(db_session)):
    settings = settings_of(request)
    name = clean_name(body.console_name)
    recovery = new_code(12)
    account = Account(id=new_id(), created_at=utcnow(), recovery_hash=hash_secret(settings.token_pepper, recovery), quota_bytes=settings.quota_bytes)
    db.add(account)
    db.flush()
    device, token = issue_device(db, settings, account.id, name, body.console_id)
    db.commit()
    log.info("account created id=%s", account.id)
    return {
        "account_id": account.id,
        "recovery_code": format_recovery(recovery),
        "device": device_out(device, token),
    }


@router.post("/v1/pairing/codes", status_code=201)
def create_code(request: Request, device: Device = Depends(current_device), db: Session = Depends(db_session)):
    settings = settings_of(request)
    code = new_code(8)
    row = PairingCode(
        id=new_id(),
        account_id=device.account_id,
        code_hash=hash_secret(settings.token_pepper, code),
        expires_at=utcnow() + timedelta(seconds=settings.pairing_ttl_seconds),
        used_at=None,
        created_by=device.id,
    )
    db.add(row)
    db.commit()
    return {"code": code, "expires_in": settings.pairing_ttl_seconds}


@router.post("/v1/pairing/redeem")
def redeem(body: RedeemIn, request: Request, db: Session = Depends(db_session)):
    settings = settings_of(request)
    ip_key = f"pair:{client_ip(request)}"
    check_rate(db, ip_key, settings)
    code = normalize_code(body.code)
    if len(code) != 8:
        record_failure(db, ip_key, settings)
        raise HTTPException(status_code=400, detail="Invalid or expired sync code")
    digest = hash_secret(settings.token_pepper, code)
    row = db.scalar(select(PairingCode).where(PairingCode.code_hash == digest).with_for_update())
    now = utcnow()
    if row is None or row.used_at is not None or row.expires_at <= now:
        record_failure(db, ip_key, settings)
        raise HTTPException(status_code=400, detail="Invalid or expired sync code")
    row.used_at = now
    poll = new_token()
    req = PairingRequest(
        id=new_id(),
        account_id=row.account_id,
        code_id=row.id,
        console_name=clean_name(body.console_name),
        console_id=body.console_id[:64],
        poll_token_hash=hash_secret(settings.token_pepper, poll),
        status="pending",
        device_id=None,
        delivery_token=None,
        created_at=now,
    )
    db.add(req)
    db.commit()
    clear_failures(db, ip_key)
    return {"status": "pending", "request_id": req.id, "poll_token": poll}


def load_request_for_poll(db: Session, settings: Settings, request_id: str, poll_token: str) -> PairingRequest:
    row = db.get(PairingRequest, request_id)
    if row is None or hash_secret(settings.token_pepper, poll_token) != row.poll_token_hash:
        raise HTTPException(status_code=404, detail="Pairing request not found")
    return row


@router.get("/v1/pairing/requests/{request_id}")
def poll_request(request_id: str, poll_token: str, request: Request, db: Session = Depends(db_session)):
    row = load_request_for_poll(db, settings_of(request), request_id, poll_token)
    payload = {"status": row.status, "request_id": row.id}
    if row.status == "approved" and row.delivery_token and row.device_id:
        payload["account_id"] = row.account_id
        payload["device"] = {"device_id": row.device_id, "token": row.delivery_token}
    return payload


@router.post("/v1/pairing/requests/{request_id}/ack")
def ack_request(request_id: str, poll_token: str, request: Request, db: Session = Depends(db_session)):
    row = load_request_for_poll(db, settings_of(request), request_id, poll_token)
    row.delivery_token = None
    db.commit()
    return {"status": "ok"}


@router.get("/v1/pairing/requests")
def list_requests(device: Device = Depends(current_device), db: Session = Depends(db_session)):
    rows = db.scalars(select(PairingRequest).where(PairingRequest.account_id == device.account_id, PairingRequest.status == "pending").order_by(PairingRequest.created_at)).all()
    return {"requests": [{"request_id": row.id, "console_name": row.console_name, "console_id": row.console_id, "created_at": iso(row.created_at)} for row in rows]}


def owned_request(db: Session, device: Device, request_id: str) -> PairingRequest:
    row = db.get(PairingRequest, request_id)
    if row is None or row.account_id != device.account_id:
        raise HTTPException(status_code=404, detail="Pairing request not found")
    return row


@router.post("/v1/pairing/requests/{request_id}/approve")
def approve(request_id: str, request: Request, device: Device = Depends(current_device), db: Session = Depends(db_session)):
    row = owned_request(db, device, request_id)
    if row.status != "pending":
        raise HTTPException(status_code=409, detail="Pairing request is not pending")
    settings = settings_of(request)
    new_device, token = issue_device(db, settings, row.account_id, row.console_name, row.console_id)
    row.status = "approved"
    row.device_id = new_device.id
    row.delivery_token = token
    db.commit()
    log.info("pairing approved account=%s device=%s", row.account_id, new_device.id)
    return {"status": "approved", "device_id": new_device.id}


@router.post("/v1/pairing/requests/{request_id}/deny")
def deny(request_id: str, device: Device = Depends(current_device), db: Session = Depends(db_session)):
    row = owned_request(db, device, request_id)
    if row.status != "pending":
        raise HTTPException(status_code=409, detail="Pairing request is not pending")
    row.status = "denied"
    db.commit()
    return {"status": "denied"}


@router.post("/v1/recovery/redeem")
def recover(body: RecoveryIn, request: Request, db: Session = Depends(db_session)):
    settings = settings_of(request)
    ip_key = f"recovery:{client_ip(request)}"
    check_rate(db, ip_key, settings)
    code = normalize_code(body.recovery_code)
    if len(code) != 12:
        record_failure(db, ip_key, settings)
        raise HTTPException(status_code=400, detail="Invalid recovery code")
    digest = hash_secret(settings.token_pepper, code)
    account = db.scalar(select(Account).where(Account.recovery_hash == digest))
    if account is None:
        record_failure(db, ip_key, settings)
        raise HTTPException(status_code=400, detail="Invalid recovery code")
    name = clean_name(body.console_name)
    device, token = issue_device(db, settings, account.id, name, body.console_id)
    fresh = new_code(12)
    account.recovery_hash = hash_secret(settings.token_pepper, fresh)
    db.commit()
    clear_failures(db, ip_key)
    log.info("account recovered id=%s", account.id)
    return {
        "account_id": account.id,
        "recovery_code": format_recovery(fresh),
        "device": device_out(device, token),
    }


@router.post("/v1/recovery/rotate")
def rotate_recovery(request: Request, device: Device = Depends(current_device), db: Session = Depends(db_session)):
    settings = settings_of(request)
    account = db.get(Account, device.account_id)
    if account is None:
        raise HTTPException(status_code=404, detail="Account not found")
    fresh = new_code(12)
    account.recovery_hash = hash_secret(settings.token_pepper, fresh)
    db.commit()
    return {"recovery_code": format_recovery(fresh)}


@router.get("/v1/devices")
def list_devices(device: Device = Depends(current_device), db: Session = Depends(db_session)):
    rows = db.scalars(select(Device).where(Device.account_id == device.account_id).order_by(Device.created_at)).all()
    return {"devices": [{**device_out(row), "this_console": row.id == device.id} for row in rows]}


@router.delete("/v1/devices/me")
def unpair_self(device: Device = Depends(current_device), db: Session = Depends(db_session)):
    device.revoked_at = utcnow()
    db.commit()
    return {"status": "revoked", "device_id": device.id}


@router.delete("/v1/devices/{device_id}")
def revoke_device(device_id: str, device: Device = Depends(current_device), db: Session = Depends(db_session)):
    target = db.get(Device, device_id)
    if target is None or target.account_id != device.account_id:
        raise HTTPException(status_code=404, detail="Console not found")
    if target.revoked_at is None:
        target.revoked_at = utcnow()
        db.commit()
    return {"status": "revoked", "device_id": target.id}


def account_usage(db: Session, account_id: str) -> int:
    rows = db.scalars(select(SaveVersion).where(SaveVersion.account_id == account_id)).all()
    return sum(row.byte_size for row in rows)


def version_out(row: SaveVersion) -> dict:
    return {
        "id": row.id,
        "title_id": row.title_id,
        "byte_size": row.byte_size,
        "checksum": row.checksum,
        "content_checksum": row.content_checksum,
        "created_at": iso(row.created_at),
        "device_id": row.device_id,
    }


@router.get("/v1/saves")
def list_saves(device: Device = Depends(current_device), db: Session = Depends(db_session)):
    rows = db.scalars(select(SaveVersion).where(SaveVersion.account_id == device.account_id).order_by(SaveVersion.created_at.desc())).all()
    latest: dict[str, SaveVersion] = {}
    for row in rows:
        latest.setdefault(row.title_id, row)
    return {"saves": [version_out(row) for row in latest.values()], "used_bytes": account_usage(db, device.account_id)}


@router.get("/v1/saves/{title_id}")
def latest_save(title_id: str, device: Device = Depends(current_device), db: Session = Depends(db_session)):
    title_id = clean_title(title_id)
    row = db.scalar(
        select(SaveVersion)
        .where(SaveVersion.account_id == device.account_id, SaveVersion.title_id == title_id)
        .order_by(SaveVersion.created_at.desc())
        .limit(1)
    )
    if row is None:
        raise HTTPException(status_code=404, detail="No cloud save for this game")
    return version_out(row)


@router.get("/v1/saves/{title_id}/versions")
def list_versions(title_id: str, device: Device = Depends(current_device), db: Session = Depends(db_session)):
    title_id = clean_title(title_id)
    rows = db.scalars(
        select(SaveVersion)
        .where(SaveVersion.account_id == device.account_id, SaveVersion.title_id == title_id)
        .order_by(SaveVersion.created_at.desc())
    ).all()
    return {"versions": [version_out(row) for row in rows]}


@router.post("/v1/saves/{title_id}/uploads", status_code=201)
def start_upload(title_id: str, body: UploadStartIn, request: Request, device: Device = Depends(current_device), db: Session = Depends(db_session)):
    settings = settings_of(request)
    title_id = clean_title(title_id)
    if body.byte_size > settings.max_save_bytes:
        raise HTTPException(status_code=413, detail="Save is larger than the server allows")
    if account_usage(db, device.account_id) + body.byte_size > settings.quota_bytes:
        raise HTTPException(status_code=413, detail="Account storage quota exceeded")
    if not re.fullmatch(r"[0-9a-f]{64}", body.checksum_sha256) or not re.fullmatch(r"[0-9a-f]{64}", body.content_sha256):
        raise HTTPException(status_code=422, detail="Checksum must be lowercase SHA-256 hex")
    upload = UploadSession(
        id=new_id(),
        account_id=device.account_id,
        title_id=title_id,
        expected_size=body.byte_size,
        expected_checksum=body.checksum_sha256,
        content_checksum=body.content_sha256,
        received=0,
        storage_name=f"{new_id()}.partial",
        device_id=device.id,
        expires_at=utcnow() + timedelta(seconds=settings.upload_ttl_seconds),
        status="open",
    )
    path = temp_path(settings.storage_dir, upload.id)
    with open(path, "wb") as handle:
        handle.truncate(0)
    db.add(upload)
    db.commit()
    return {"upload_id": upload.id, "chunk_size": settings.chunk_size, "received": 0}


def open_upload(db: Session, device: Device, upload_id: str) -> UploadSession:
    row = db.get(UploadSession, upload_id)
    if row is None or row.account_id != device.account_id:
        raise HTTPException(status_code=404, detail="Upload not found")
    if row.status != "open":
        raise HTTPException(status_code=409, detail="Upload is closed")
    if row.expires_at <= utcnow():
        row.status = "expired"
        db.commit()
        raise HTTPException(status_code=409, detail="Upload expired")
    return row


@router.get("/v1/uploads/{upload_id}")
def upload_status(upload_id: str, device: Device = Depends(current_device), db: Session = Depends(db_session)):
    row = db.get(UploadSession, upload_id)
    if row is None or row.account_id != device.account_id:
        raise HTTPException(status_code=404, detail="Upload not found")
    return {"upload_id": row.id, "received": row.received, "expected_size": row.expected_size, "status": row.status}


@router.put("/v1/uploads/{upload_id}/chunks")
async def upload_chunk(upload_id: str, request: Request, offset: int, device: Device = Depends(current_device), db: Session = Depends(db_session)):
    row = open_upload(db, device, upload_id)
    if offset != row.received:
        raise HTTPException(status_code=409, detail="Chunk offset does not continue the upload")
    if offset < 0:
        raise HTTPException(status_code=422, detail="Bad offset")
    body = await request.body()
    if not body:
        raise HTTPException(status_code=422, detail="Empty chunk")
    if row.received + len(body) > row.expected_size:
        raise HTTPException(status_code=413, detail="Upload exceeds declared size")
    settings = settings_of(request)
    path = temp_path(settings.storage_dir, row.id)
    with open(path, "r+b") as handle:
        handle.seek(offset)
        handle.write(body)
        handle.flush()
        os.fsync(handle.fileno())
    row.received += len(body)
    db.commit()
    return {"received": row.received, "complete": row.received == row.expected_size}


def prune_versions(db: Session, settings: Settings, account_id: str, title_id: str) -> None:
    rows = db.scalars(
        select(SaveVersion)
        .where(SaveVersion.account_id == account_id, SaveVersion.title_id == title_id)
        .order_by(SaveVersion.created_at.desc())
    ).all()
    for old in rows[settings.retention_versions :]:
        path = final_path(settings.storage_dir, account_id, old.storage_name)
        if os.path.exists(path):
            os.remove(path)
        db.delete(old)


@router.post("/v1/uploads/{upload_id}/commit")
def commit_upload(upload_id: str, request: Request, device: Device = Depends(current_device), db: Session = Depends(db_session)):
    settings = settings_of(request)
    row = open_upload(db, device, upload_id)
    if row.received != row.expected_size:
        raise HTTPException(status_code=409, detail="Upload is incomplete")
    src = temp_path(settings.storage_dir, row.id)
    if not os.path.exists(src):
        row.status = "failed"
        db.commit()
        raise HTTPException(status_code=409, detail="Upload data is missing")
    digest = file_checksum(src)
    if digest != row.expected_checksum:
        row.status = "failed"
        db.commit()
        os.remove(src)
        raise HTTPException(status_code=422, detail="Checksum does not match the uploaded save")
    with open(src, "rb") as handle:
        magic = handle.read(4)
    if magic != b"2DSV":
        row.status = "failed"
        db.commit()
        os.remove(src)
        raise HTTPException(status_code=422, detail="File is not a 2DSync save container")
    storage_name = f"{row.title_id}-{new_id()}.2dsv"
    dst = final_path(settings.storage_dir, row.account_id, storage_name)
    atomic_replace(src, dst)
    version = SaveVersion(
        id=new_id(),
        account_id=row.account_id,
        title_id=row.title_id,
        byte_size=row.expected_size,
        checksum=digest,
        content_checksum=row.content_checksum,
        storage_name=storage_name,
        device_id=device.id,
        created_at=utcnow(),
    )
    db.add(version)
    row.status = "committed"
    db.flush()
    prune_versions(db, settings, row.account_id, row.title_id)
    db.commit()
    log.info("save stored account=%s title=%s version=%s bytes=%s", row.account_id, row.title_id, version.id, version.byte_size)
    return version_out(version)


def owned_version(db: Session, device: Device, title_id: str, version_id: str) -> SaveVersion:
    title_id = clean_title(title_id)
    row = db.get(SaveVersion, version_id)
    if row is None or row.account_id != device.account_id or row.title_id != title_id:
        raise HTTPException(status_code=404, detail="Save version not found")
    return row


@router.get("/v1/saves/{title_id}/versions/{version_id}/download")
def download_version(title_id: str, version_id: str, request: Request, device: Device = Depends(current_device), db: Session = Depends(db_session)):
    row = owned_version(db, device, title_id, version_id)
    path = final_path(settings_of(request).storage_dir, row.account_id, row.storage_name)
    if not os.path.isfile(path):
        raise HTTPException(status_code=404, detail="Save file is missing")
    return FileResponse(path, media_type="application/octet-stream", filename=f"{row.title_id}.2dsv", headers={"X-Checksum-Sha256": row.checksum, "X-Title-Id": row.title_id})


def create_app(settings: Settings | None = None) -> FastAPI:
    settings = settings or Settings.from_env()
    settings.ensure_dirs()
    engine = make_engine(settings)
    init_database(engine)
    app = FastAPI(
        title="2DSync",
        version="0.1.0",
        description="Private cloud save sync for Nintendo 3DS family consoles. Save files are account-scoped and are not part of the public source tree.",
    )
    app.state.settings = settings
    app.state.engine = engine
    app.state.Session = make_session_factory(engine)

    @app.middleware("http")
    async def log_requests(request: Request, call_next):
        response = await call_next(request)
        log.info("%s %s -> %s", request.method, request.url.path, response.status_code)
        return response

    app.include_router(router)
    return app
