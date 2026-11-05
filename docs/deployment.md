# Deployment

Run this on the Linux machine that will hold the saves. The API is not exposed directly. Caddy terminates HTTPS and proxies to the API container.

```bash
cp docker/.env.example docker/.env
```

Set:

- `POSTGRES_PASSWORD` to a long password.
- `TOKEN_PEPPER` to a long random string. It hashes device tokens, sync codes, and recovery codes. Losing it invalidates every login. Keep a copy with your server backups.
- `DOMAIN` to the name that points at this machine.

Then:

```bash
docker compose -f docker/docker-compose.yml --env-file docker/.env up -d --build
```

Tables are created when the API starts. Save bytes are stored in the `saves` Docker volume, under `/data/saves`, which is not a public web directory. Caddy's public site only reverse-proxies the API.

On the console, Settings -> Server Address must be `https://your-domain`. Then use Test Connection.

## HTTPS and the 3DS

The 3DS TLS stack trusts an old set of roots (Nintendo, USERTrust, DigiCert, and a few others). A typical Let's Encrypt certificate will not verify. Two supported choices:

1. Leave TLS verification off in Settings. The traffic is still encrypted. You are not checking that the certificate belongs to your server. Use this only for a server you control.
2. Put your CA certificate, DER encoded, on the SD card as `sdmc:/3ds/2DSync/root.der` and turn TLS verification on. 2DSync passes that certificate to `httpcAddTrustedRootCA`.

The server should offer TLS 1.2. Very old consoles have a small cipher list. If Test Connection fails with verification off, try another TLS terminator or an older cipher suite on that host. Do not fall back to plain HTTP. The client only accepts `https://` addresses.

## Quotas and retention

Each account defaults to 256 MiB. Each save container defaults to a 32 MiB maximum. The ten newest versions of each title are kept. Both limits are environment variables on the API.

## Backup

Stop is not required for a consistent copy if you dump the database and the volume together, but a short maintenance window is simpler.

```bash
docker compose -f docker/docker-compose.yml --env-file docker/.env exec db \
  pg_dump -U 2dsync 2dsync > 2dsync.sql
docker run --rm -v docker_saves:/data -v "$PWD":/backup alpine \
  tar -C /data -czf /backup/saves.tgz .
```

Restore by loading `2dsync.sql` into a new database and extracting `saves.tgz` into an empty saves volume. The pepper must be the same one from the old `.env`, or existing tokens will not match.

Also keep the recovery codes your consoles displayed. A database backup without those codes still restores the saves for consoles that are already paired. A new console needs a recovery code, or a paired console that can approve it.

## Logs

The API logs the method, path, and status. It does not log tokens, sync codes, recovery codes, or save bytes.
