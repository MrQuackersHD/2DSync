# Save format

2DSync copies the files inside a title's save archive. It does not copy the encrypted save image from the Nintendo 3DS directory on the SD card, and it does not copy the game.

For a digital title or a cartridge whose save is a filesystem, the client opens `ARCHIVE_USER_SAVEDATA` with a 12-byte binary path: media type, title ID low, title ID high. Extra data, when the title has an ID from `AM_GetTitleExtDataId` and the archive opens, is copied too. After a restore the client calls `FSUSER_ControlArchive` with `ARCHIVE_ACTION_COMMIT_SAVE_DATA`.

Cartridge saves that are only a SPI flash chip (`ARCHIVE_CARD_SPIFS`) are left alone and shown as unsupported. Writing a raw flash dump back to the wrong chip can destroy that cartridge's save.

## Container

A `.2dsv` file is the transfer format.

- Magic `2DSV`, version 1.
- Title ID, optional secure value, and the time the container was packed.
- A short JSON file list.
- `savedata/...` and `extdata/...` files, then a zero end marker.

Paths containing `..` or a drive prefix are rejected. A container may hold at most 256 files, 8 MiB each, and 32 MiB total.

The upload has two checksums:

- `checksum_sha256` is the SHA-256 of the container bytes. The server refuses the upload if the bytes do not match, and it does not replace the previous version.
- `content_sha256` is the SHA-256 of the sorted relative paths, sizes, and file bytes. The console uses this to decide whether the local save and the cloud save are the same. The container timestamp is not part of it, so packing the same save twice does not look like a conflict.

Chunks must arrive in order. An unfinished upload stays in a temporary file. A restart of the server does not promote that file to a version.

## Secure value

Some SD games, including several Pokémon titles, Animal Crossing: New Leaf, and Super Smash Bros., store a secure value that the game compares with the console. 2DSync reads `FSUSER_GetSaveDataSecureValue` while exporting an SD save and writes it back with `FSUSER_SetSaveDataSecureValue` after a successful commit. Cartridge games generally do not use this slot.

That makes a normal cross-console restore possible for titles that only check this value. It does not guarantee every game will accept the save. If a game deletes the restored data and starts over, use Restore Backup. 2DSync creates that backup before it overwrites a local save.

## Conflict rule

A newer timestamp does not win. If both saves exist and the content checksums differ, the console asks. If they match, it reports that they are already in sync. If the cloud cannot be reached, cloud actions stop and the local save stays where it is.
