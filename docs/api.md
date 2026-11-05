# HTTP API

Base URL example: `https://sync.example.com`

Authenticated requests send `Authorization: Bearer <device token>`. The sync code and the recovery code are not sent as that token.

| Method | Path | Auth | Purpose |
| --- | --- | --- | --- |
| GET | `/health` | no | Database and storage are writable |
| POST | `/v1/accounts` | no | Create an account, return a recovery code and the first device token |
| POST | `/v1/pairing/codes` | yes | Create an 8-character code, lifetime `PAIRING_TTL_SECONDS` (600) |
| POST | `/v1/pairing/redeem` | no | Exchange a code for a pending request and a poll token |
| GET | `/v1/pairing/requests` | yes | List consoles waiting for approval |
| POST | `/v1/pairing/requests/{id}/approve` | yes | Issue that console its own device token |
| POST | `/v1/pairing/requests/{id}/deny` | yes | Reject the request. The code is already spent |
| GET | `/v1/pairing/requests/{id}?poll_token=` | poll token | Read pending, approved, or denied. The device token is returned until ack |
| POST | `/v1/pairing/requests/{id}/ack?poll_token=` | poll token | Forget the delivered device token |
| POST | `/v1/recovery/redeem` | no | Pair a new console with a recovery code. The code rotates |
| POST | `/v1/recovery/rotate` | yes | Replace the recovery code while a console is still paired |
| GET | `/v1/devices` | yes | List consoles on this account |
| DELETE | `/v1/devices/me` | yes | Unpair this console |
| DELETE | `/v1/devices/{id}` | yes | Revoke one console on this account |
| GET | `/v1/saves` | yes | Latest version of each title |
| GET | `/v1/saves/{title_id}` | yes | Latest version, or 404 |
| GET | `/v1/saves/{title_id}/versions` | yes | History, newest first |
| POST | `/v1/saves/{title_id}/uploads` | yes | Start an upload. Body: `byte_size`, `checksum_sha256`, `content_sha256` |
| GET | `/v1/uploads/{id}` | yes | Bytes received so far |
| PUT | `/v1/uploads/{id}/chunks?offset=` | yes | Raw body. `offset` must equal the bytes already stored |
| POST | `/v1/uploads/{id}/commit` | yes | Check the container magic and checksum, then publish a version |
| GET | `/v1/saves/{title_id}/versions/{id}/download` | yes | File body plus `X-Checksum-Sha256` |

Title IDs are 16 hexadecimal characters. A failed sync code or recovery code is counted per client address. After five failures the address is locked for 15 minutes. Wrong codes and expired codes return the same "invalid" error.

Uploads that fail the checksum, exceed the quota, or never get a commit do not replace the current version. OpenAPI for the running server is `/docs`.
