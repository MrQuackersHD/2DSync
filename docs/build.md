# Build

Install devkitPro's `3ds-dev` group. The environment needs `DEVKITPRO` and `DEVKITARM`.

```bash
python3 scripts/gen_assets.py
make -C 3ds-client
```

`scripts/gen_assets.py` writes `3ds-client/icon.png`, `banner.png`, and `banner.wav`. `bannertool` turns those into the CIA banner. `makerom` builds the CIA from `3ds-client/app.rsf`.

Current devkitPro packages include devkitARM, libctru, citro2d, and `3dsxtool`. They do not include `bannertool` or `makerom`. Without those two commands on `PATH`, `make` still produces `2DSync.3dsx` and prints that the CIA was skipped. Build [makerom](https://github.com/3DSGuy/Project_CTR) and [bannertool](https://github.com/diasurgical/bannertool) yourself, put them on `PATH`, and run `make -C 3ds-client` again.

The CIA asks for filesystem-tool access so it can open another title's user save archive (`ARCHIVE_USER_SAVEDATA`) and extra data archive (`ARCHIVE_EXTDATA`). Custom firmware is required to install that CIA. The application unique ID is `0x2D59C`.

Host-side logic does not need devkitARM:

```bash
make -C tests/host test
```

## Server

```bash
cd server
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
python3 -m pytest -q
```

SQLite is the default database for a single small deployment. PostgreSQL is the database to use when the service is shared or restarted on a VPS. Both use the same SQLAlchemy models through `DATABASE_URL`.
