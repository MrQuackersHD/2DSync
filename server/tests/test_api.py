import io
import os
import struct
from datetime import datetime, timedelta, timezone

import pytest
from fastapi.testclient import TestClient

from app.models import PairingCode, UploadSession
from app.routes import create_app
from app.settings import Settings


def client_for(tmp_path, **overrides):
    data = dict(
        database_url=f"sqlite:///{tmp_path / 'test.db'}",
        storage_dir=str(tmp_path / "saves"),
        token_pepper="pepper-for-tests",
        pairing_ttl_seconds=600,
        max_failed_attempts=5,
        lockout_seconds=900,
        quota_bytes=200,
        max_save_bytes=128,
        retention_versions=2,
        chunk_size=8,
        upload_ttl_seconds=3600,
    )
    data.update(overrides)
    app = create_app(Settings(**data))
    return TestClient(app)


def make_container(title: int = 0x0004000000123400, payload: bytes = b"SAVE") -> bytes:
    manifest = b'{"files":["savedata/data.bin"]}'
    buf = io.BytesIO()
    buf.write(b"2DSV")
    buf.write(struct.pack("<HH", 1, 0))
    buf.write(struct.pack("<QQQ", title, 0, 1))
    buf.write(struct.pack("<I", len(manifest)))
    buf.write(manifest)
    rel = b"savedata/data.bin"
    buf.write(bytes([1]))
    buf.write(struct.pack("<H", len(rel)))
    buf.write(rel)
    buf.write(struct.pack("<I", len(payload)))
    buf.write(payload)
    buf.write(bytes([0]))
    return buf.getvalue()


def auth(token):
    return {"Authorization": f"Bearer {token}"}


def upload(client, token, blob, title="0004000000123400"):
    import hashlib

    digest = hashlib.sha256(blob).hexdigest()
    started = client.post(
        f"/v1/saves/{title}/uploads",
        headers=auth(token),
        json={"byte_size": len(blob), "checksum_sha256": digest, "content_sha256": digest},
    )
    assert started.status_code == 201, started.text
    upload_id = started.json()["upload_id"]
    chunk = started.json()["chunk_size"]
    offset = 0
    while offset < len(blob):
        piece = blob[offset : offset + chunk]
        put = client.put(f"/v1/uploads/{upload_id}/chunks", params={"offset": offset}, headers=auth(token), content=piece)
        assert put.status_code == 200, put.text
        offset += len(piece)
    committed = client.post(f"/v1/uploads/{upload_id}/commit", headers=auth(token))
    return committed


def test_health_and_account(tmp_path):
    client = client_for(tmp_path)
    assert client.get("/health").json()["status"] == "ok"
    created = client.post("/v1/accounts", json={"console_name": "Console A", "console_id": "console-a-0001"})
    assert created.status_code == 201
    body = created.json()
    assert len(body["recovery_code"].replace("-", "")) == 12
    assert body["device"]["token"]
    me = client.get("/v1/devices", headers=auth(body["device"]["token"]))
    assert me.status_code == 200
    assert me.json()["devices"][0]["this_console"] is True


def test_pairing_approval_and_invalid_codes(tmp_path):
    client = client_for(tmp_path)
    owner = client.post("/v1/accounts", json={"console_name": "Console A", "console_id": "console-a-0001"}).json()
    token = owner["device"]["token"]
    code = client.post("/v1/pairing/codes", headers=auth(token)).json()["code"]
    bad = client.post("/v1/pairing/redeem", json={"code": "ZZZZZZZZ", "console_name": "Console B", "console_id": "console-b-0001"})
    assert bad.status_code == 400

    pending = client.post("/v1/pairing/redeem", json={"code": code, "console_name": "Console B", "console_id": "console-b-0001"})
    assert pending.status_code == 200
    request_id = pending.json()["request_id"]
    poll_token = pending.json()["poll_token"]
    again = client.post("/v1/pairing/redeem", json={"code": code, "console_name": "Console C", "console_id": "console-c-0001"})
    assert again.status_code == 400

    listed = client.get("/v1/pairing/requests", headers=auth(token))
    assert listed.json()["requests"][0]["console_name"] == "Console B"
    approved = client.post(f"/v1/pairing/requests/{request_id}/approve", headers=auth(token))
    assert approved.status_code == 200
    polled = client.get(f"/v1/pairing/requests/{request_id}", params={"poll_token": poll_token})
    assert polled.json()["status"] == "approved"
    other = polled.json()["device"]["token"]
    polled_again = client.get(f"/v1/pairing/requests/{request_id}", params={"poll_token": poll_token})
    assert polled_again.json()["device"]["token"] == other
    client.post(f"/v1/pairing/requests/{request_id}/ack", params={"poll_token": poll_token})
    assert "device" not in client.get(f"/v1/pairing/requests/{request_id}", params={"poll_token": poll_token}).json()

    saves = client.get("/v1/saves", headers=auth(other))
    assert saves.status_code == 200
    stranger = client.get("/v1/saves", headers=auth("not-a-real-token"))
    assert stranger.status_code == 401


def test_pairing_expiration(tmp_path):
    client = client_for(tmp_path, pairing_ttl_seconds=600)
    owner = client.post("/v1/accounts", json={"console_name": "Console A", "console_id": "console-a-0001"}).json()
    code = client.post("/v1/pairing/codes", headers=auth(owner["device"]["token"])).json()["code"]
    with client.app.state.Session() as db:
        row = db.query(PairingCode).one()
        row.expires_at = datetime.now(timezone.utc).replace(tzinfo=None) - timedelta(seconds=5)
        db.commit()
    expired = client.post("/v1/pairing/redeem", json={"code": code, "console_name": "Console B", "console_id": "console-b-0001"})
    assert expired.status_code == 400


def test_rate_limit_invalid_codes(tmp_path):
    client = client_for(tmp_path, max_failed_attempts=3)
    for _ in range(3):
        response = client.post("/v1/pairing/redeem", json={"code": "AAAAAAAA", "console_name": "Console B", "console_id": "console-b-0001"})
        assert response.status_code == 400
    blocked = client.post("/v1/pairing/redeem", json={"code": "BBBBBBBB", "console_name": "Console B", "console_id": "console-b-0001"})
    assert blocked.status_code == 429


def test_deny_and_revoke_and_recovery(tmp_path):
    client = client_for(tmp_path)
    owner = client.post("/v1/accounts", json={"console_name": "Console A", "console_id": "console-a-0001"}).json()
    token = owner["device"]["token"]
    recovery = owner["recovery_code"]
    code = client.post("/v1/pairing/codes", headers=auth(token)).json()["code"]
    pending = client.post("/v1/pairing/redeem", json={"code": code, "console_name": "Console B", "console_id": "console-b-0001"}).json()
    denied = client.post(f"/v1/pairing/requests/{pending['request_id']}/deny", headers=auth(token))
    assert denied.json()["status"] == "denied"
    polled = client.get(f"/v1/pairing/requests/{pending['request_id']}", params={"poll_token": pending["poll_token"]})
    assert polled.json()["status"] == "denied"

    code = client.post("/v1/pairing/codes", headers=auth(token)).json()["code"]
    pending = client.post("/v1/pairing/redeem", json={"code": code, "console_name": "Console B", "console_id": "console-b-0002"}).json()
    client.post(f"/v1/pairing/requests/{pending['request_id']}/approve", headers=auth(token))
    other = client.get(f"/v1/pairing/requests/{pending['request_id']}", params={"poll_token": pending["poll_token"]}).json()["device"]
    devices = client.get("/v1/devices", headers=auth(token)).json()["devices"]
    assert len(devices) == 2
    revoked = client.delete(f"/v1/devices/{other['device_id']}", headers=auth(token))
    assert revoked.status_code == 200
    assert client.get("/v1/saves", headers=auth(other["token"])).status_code == 401

    client.delete("/v1/devices/me", headers=auth(token))
    assert client.get("/v1/devices", headers=auth(token)).status_code == 401
    recovered = client.post("/v1/recovery/redeem", json={"recovery_code": recovery, "console_name": "Console C", "console_id": "console-c-0001"})
    assert recovered.status_code == 200
    new_recovery = recovered.json()["recovery_code"]
    new_token = recovered.json()["device"]["token"]
    assert client.get("/v1/devices", headers=auth(new_token)).status_code == 200
    reused = client.post("/v1/recovery/redeem", json={"recovery_code": recovery, "console_name": "Console D", "console_id": "console-d-0001"})
    assert reused.status_code == 400
    rotated = client.post("/v1/recovery/rotate", headers=auth(new_token))
    assert rotated.status_code == 200
    assert rotated.json()["recovery_code"] != new_recovery


def test_upload_download_integrity_and_interruption(tmp_path):
    client = client_for(tmp_path, quota_bytes=10_000, max_save_bytes=10_000, chunk_size=4)
    token = client.post("/v1/accounts", json={"console_name": "Console A", "console_id": "console-a-0001"}).json()["device"]["token"]
    blob = make_container(payload=b"FIRST-SAVE")
    committed = upload(client, token, blob)
    assert committed.status_code == 200, committed.text
    version = committed.json()
    downloaded = client.get(f"/v1/saves/{version['title_id']}/versions/{version['id']}/download", headers=auth(token))
    assert downloaded.status_code == 200
    assert downloaded.content == blob
    assert downloaded.headers["x-checksum-sha256"] == version["checksum"]

    import hashlib

    bad = client.post(
        "/v1/saves/0004000000123400/uploads",
        headers=auth(token),
        json={"byte_size": len(blob), "checksum_sha256": hashlib.sha256(b"nope").hexdigest(), "content_sha256": version["content_checksum"]},
    )
    upload_id = bad.json()["upload_id"]
    offset = 0
    while offset < len(blob):
        client.put(f"/v1/uploads/{upload_id}/chunks", params={"offset": offset}, headers=auth(token), content=blob[offset : offset + 4])
        offset += 4
    rejected = client.post(f"/v1/uploads/{upload_id}/commit", headers=auth(token))
    assert rejected.status_code == 422
    latest = client.get("/v1/saves/0004000000123400", headers=auth(token)).json()
    assert latest["checksum"] == version["checksum"]

    started = client.post(
        "/v1/saves/0004000000123400/uploads",
        headers=auth(token),
        json={"byte_size": len(blob), "checksum_sha256": version["checksum"], "content_sha256": version["content_checksum"]},
    )
    partial_id = started.json()["upload_id"]
    client.put(f"/v1/uploads/{partial_id}/chunks", params={"offset": 0}, headers=auth(token), content=blob[:4])
    incomplete = client.post(f"/v1/uploads/{partial_id}/commit", headers=auth(token))
    assert incomplete.status_code == 409
    status = client.get(f"/v1/uploads/{partial_id}", headers=auth(token)).json()
    assert status["received"] == 4
    assert client.get("/v1/saves/0004000000123400", headers=auth(token)).json()["id"] == version["id"]


def test_multiple_consoles_history_quota_and_restart(tmp_path):
    client = client_for(tmp_path, quota_bytes=10_000, max_save_bytes=10_000, retention_versions=2, chunk_size=64)
    created = client.post("/v1/accounts", json={"console_name": "Console A", "console_id": "console-a-0001"}).json()
    token_a = created["device"]["token"]
    code = client.post("/v1/pairing/codes", headers=auth(token_a)).json()["code"]
    pending = client.post("/v1/pairing/redeem", json={"code": code, "console_name": "Console B", "console_id": "console-b-0001"}).json()
    request_id = pending["request_id"]
    client.post(f"/v1/pairing/requests/{request_id}/approve", headers=auth(token_a))
    token_b = client.get(f"/v1/pairing/requests/{request_id}", params={"poll_token": pending["poll_token"]}).json()["device"]["token"]

    first = upload(client, token_a, make_container(payload=b"AAA")).json()
    second = upload(client, token_b, make_container(payload=b"BBB-NEW")).json()
    assert first["checksum"] != second["checksum"]
    versions = client.get("/v1/saves/0004000000123400/versions", headers=auth(token_a)).json()["versions"]
    assert versions[0]["id"] == second["id"]
    assert client.get(f"/v1/saves/0004000000123400/versions/{second['id']}/download", headers=auth(token_a)).content.endswith(b"BBB-NEW\x00") or b"BBB-NEW" in client.get(f"/v1/saves/0004000000123400/versions/{second['id']}/download", headers=auth(token_a)).content

    third = upload(client, token_a, make_container(payload=b"CCC!!")).json()
    versions = client.get("/v1/saves/0004000000123400/versions", headers=auth(token_b)).json()["versions"]
    assert len(versions) == 2
    assert all(item["id"] != first["id"] for item in versions)
    missing = client.get(f"/v1/saves/0004000000123400/versions/{first['id']}/download", headers=auth(token_b))
    assert missing.status_code == 404

    other = client.post("/v1/accounts", json={"console_name": "Other", "console_id": "console-z-0001"}).json()
    stolen = client.get(f"/v1/saves/0004000000123400/versions/{third['id']}/download", headers=auth(other["device"]["token"]))
    assert stolen.status_code == 404

    tiny = client_for(tmp_path / "quota", quota_bytes=20, max_save_bytes=100)
    tiny_token = tiny.post("/v1/accounts", json={"console_name": "Tiny", "console_id": "console-t-0001"}).json()["device"]["token"]
    too_big = tiny.post("/v1/saves/0004000000123400/uploads", headers=auth(tiny_token), json={"byte_size": 50, "checksum_sha256": "a" * 64, "content_sha256": "b" * 64})
    assert too_big.status_code == 413

    settings = client.app.state.settings
    restarted = TestClient(create_app(settings))
    assert restarted.get("/v1/saves/0004000000123400", headers=auth(token_a)).json()["id"] == third["id"]
    assert restarted.get("/health").status_code == 200


def test_not_a_container_and_gap(tmp_path):
    client = client_for(tmp_path, quota_bytes=10_000, max_save_bytes=10_000, chunk_size=4)
    token = client.post("/v1/accounts", json={"console_name": "Console A", "console_id": "console-a-0001"}).json()["device"]["token"]
    blob = b"this is not a save container but it is long enough"
    import hashlib

    digest = hashlib.sha256(blob).hexdigest()
    started = client.post("/v1/saves/0004000000123400/uploads", headers=auth(token), json={"byte_size": len(blob), "checksum_sha256": digest, "content_sha256": digest})
    upload_id = started.json()["upload_id"]
    gap = client.put(f"/v1/uploads/{upload_id}/chunks", params={"offset": 4}, headers=auth(token), content=blob[:4])
    assert gap.status_code == 409
    offset = 0
    while offset < len(blob):
        assert client.put(f"/v1/uploads/{upload_id}/chunks", params={"offset": offset}, headers=auth(token), content=blob[offset : offset + 4]).status_code == 200
        offset += 4
    rejected = client.post(f"/v1/uploads/{upload_id}/commit", headers=auth(token))
    assert rejected.status_code == 422
    assert client.get("/v1/saves/0004000000123400", headers=auth(token)).status_code == 404
    with client.app.state.Session() as db:
        row = db.get(UploadSession, upload_id)
        assert row.status == "failed"
