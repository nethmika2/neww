# CYD Music: links in, songs on the SD card

Replaces the "YouTube -> converter site -> copy to SD" routine with one window.

## One-time setup (Windows)
1. Double-click **Setup.bat**. It installs Python (if you don't have it), yt-dlp and ffmpeg.
   If it installs Python, close it and run Setup.bat a second time.

## Everyday use
1. Put the SD card in your PC.
2. Double-click **CYD Music.bat**.
3. Paste links, one per line. You can paste a whole block at once. You can also type a
   song name like `daft punk one more time`, and it picks the top YouTube result.
   Playlist links download the whole playlist.
4. Press Enter on an empty line. It downloads, converts and copies to the card.
5. Eject the card in Windows and put it in the CYD.

Other ways to use it:
- **Drag mp3/m4a/flac files (or a folder of them) onto `CYD Music.bat`** to convert and copy them.
- Press Enter on an empty prompt with no links to copy your library to the card.
- A `.txt` file of links (one per line) can be pasted in as a path.

## What it does for you
- Output is **44.1 kHz, 16-bit, stereo WAV**. This is the only format the firmware plays
  (`parseWavHeader` / `playTrack` in `src/AudioApp.cpp`); anything else is skipped.
- Files go in the **root of the card**, which is where `loadPlaylist()` looks.
- Names are cleaned: `Artist - Song (Official Video) [HD]` becomes `Artist - Song`.
  They're ASCII only and up to 48 characters, because the TFT font can't draw emoji or
  non-Latin characters.
- Songs are also kept in `%USERPROFILE%\Music\CYD-Music`, so you can swap cards or
  re-copy any time without downloading again. Only missing songs are copied.
- It finds the SD card automatically (the single removable drive). If you have more than
  one, it asks once and remembers. To force a drive, run `python cydmusic.py --sd E:`.
- It warns you when the card goes over `MAX_TRACKS` (100), because the player ignores the rest.
- It skips anything over 4 GB, since FAT32 can't store it.

## If downloads stop working
YouTube changes often. Double-click **Update-ytdlp.bat**.

## Notes
- Only download music you have the right to use.
- 1 minute of this WAV is about 10 MB, so a 32 GB card holds roughly 50 hours.
