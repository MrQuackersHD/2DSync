# Limitations

These cases are reported in the client. They are not treated as successful syncs.

- System titles, updates, DLC, demos, and DSiWare are listed only when "All titles" is enabled, and they cannot be backed up.
- A cartridge save that is not a filesystem (CARD1 / SPI) is not read or written.
- Save archives that fail to open show the result code. On a 3DSX build this often means the launcher cannot open another title's save. Install `2DSync.cia`.
- Empty save archives are not uploaded.
- Saves larger than 32 MiB, or a single file larger than 8 MiB, are rejected.
- The 3DS TLS library does not trust current public CAs by default. See [deployment.md](deployment.md).
- Secure-value restore covers the FS secure-value slot for SD titles. A game can still reject a save for its own reasons. The local backup made just before the restore is the recovery path.
- 2DSync does not sync while a game is the running application. Launch 2DSync from the HOME Menu after leaving the game.
- Icons come from the title's SMDH when the filesystem allows it. A missing icon does not block a backup.
- Device tokens are stored in `sdmc:/3ds/2DSync/config.txt`. Anyone with the SD card can read them. Unpair a console you no longer have, and keep the recovery code somewhere else.
- This tree does not include a hardware test log. Install the CIA on a console you own before trusting it with a save you cannot replace.
