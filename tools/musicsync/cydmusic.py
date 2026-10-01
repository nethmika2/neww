#!/usr/bin/env python3
"""CYD Music - paste YouTube links (or song names), get WAVs on your SD card.

What it does for every item you give it:
  1. downloads the audio with yt-dlp (links, playlists, or plain "artist song" text)
  2. converts it to what the CYD firmware plays: 44.1 kHz, 16-bit, stereo WAV
  3. gives it a short, plain file name (the CYD screen can't draw emoji etc.)
  4. keeps a copy in your PC library folder
  5. copies anything the SD card doesn't have yet onto the card's root folder

Also accepts local audio files (mp3, m4a, flac, ...): drag them onto the .bat.

Usage:
  python cydmusic.py                 interactive: paste links, blank line to go
  python cydmusic.py URL [URL|text]  one-shot
  python cydmusic.py --sync          only copy library -> SD card
  python cydmusic.py --sd E:         pick the card drive (remembered)
  python cydmusic.py --update        update yt-dlp (do this if downloads break)
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unicodedata
from pathlib import Path

# ---- must match the firmware (include/Config.h, src/AudioApp.cpp) ----------
SAMPLE_RATE = 44100      # the firmware only plays 44.1 kHz / 16-bit / stereo
MAX_TRACKS = 100         # MAX_TRACKS in Config.h - extra files are ignored
FAT32_MAX = 4 * 1024**3 - 1
MAX_NAME = 48            # characters, keeps titles readable on the 320x240 screen
# ----------------------------------------------------------------------------

LIBRARY = Path(os.environ.get("CYD_LIBRARY", Path.home() / "Music" / "CYD-Music"))
CONFIG = Path.home() / ".cydmusic.json"
AUDIO_EXT = {".mp3", ".m4a", ".aac", ".flac", ".ogg", ".opus", ".wav", ".wma", ".webm", ".mp4"}


# ---------------------------------------------------------------- helpers ---
def load_config():
    try:
        return json.loads(CONFIG.read_text())
    except Exception:
        return {}


def save_config(cfg):
    try:
        CONFIG.write_text(json.dumps(cfg))
    except OSError:
        pass


def clean_name(title):
    """'Artist - Song (Official Video) <emoji>' -> 'Artist - Song'. ASCII only, <= MAX_NAME."""
    t = unicodedata.normalize("NFKD", title).encode("ascii", "ignore").decode()
    # drop bracketed marketing noise: (Official Video), [Lyrics], (HD) ...
    t = re.sub(r"[\(\[][^\)\]]*(official|lyric|video|audio|visuali[sz]er|hd|4k|remaster|explicit)[^\)\]]*[\)\]]",
               "", t, flags=re.I)
    t = re.sub(r'[<>:"/\\|?*\x00-\x1f]', " ", t)
    t = re.sub(r"\s+", " ", t).strip(" .-_")
    t = t[:MAX_NAME].strip(" .-_")
    return t or "track"


def find_ffmpeg():
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        sys.exit("ffmpeg not found. Run Setup.bat first.")


def ffmpeg_args():
    # what the firmware needs; pcm_s16le makes a plain WAV header it parses
    return ["-ar", str(SAMPLE_RATE), "-ac", "2", "-acodec", "pcm_s16le"]


# ----------------------------------------------------------- download/convert
def download(item, outdir, ffmpeg):
    """Download one link / search text with yt-dlp into outdir as .wav files."""
    is_url = re.match(r"https?://", item) is not None
    target = item if is_url else f"ytsearch1:{item}"
    # a video link that also carries &list=... means "this video", not the whole mix
    single = is_url and "v=" in item and "list=" in item
    cmd = [sys.executable, "-m", "yt_dlp", target,
           "--no-playlist" if single or not is_url else "--yes-playlist",
           "--playlist-end", str(MAX_TRACKS),
           "-f", "bestaudio/best", "-x", "--audio-format", "wav",
           "--postprocessor-args", "ffmpeg:" + " ".join(ffmpeg_args()),
           "--ffmpeg-location", ffmpeg,
           "--no-warnings", "--newline",
           "-o", str(outdir / "%(title).150B.%(ext)s")]
    return subprocess.call(cmd) == 0


def convert_local(src, outdir, ffmpeg):
    out = outdir / (src.stem + ".wav")
    r = subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", str(src), "-vn", *ffmpeg_args(), str(out)])
    return r.returncode == 0


def process(items, ffmpeg, downloader=download):
    """Turn items into library WAVs. Returns list of new library paths."""
    LIBRARY.mkdir(parents=True, exist_ok=True)
    new = []
    for i, item in enumerate(items, 1):
        print(f"\n[{i}/{len(items)}] {item}")
        with tempfile.TemporaryDirectory(prefix="cydmusic_") as tmp:
            tmp = Path(tmp)
            p = Path(item.strip('"'))
            ok = convert_local(p, tmp, ffmpeg) if p.is_file() else downloader(item, tmp, ffmpeg)
            wavs = sorted(tmp.glob("*.wav"))
            if not ok and not wavs:
                print("   !! failed (bad link? if it keeps happening, run Update-ytdlp.bat)")
                continue
            for w in wavs:
                if w.stat().st_size > FAT32_MAX:
                    print(f"   !! {w.stem}: over 4 GB, can't live on a FAT32 card - skipped")
                    continue
                dest = LIBRARY / f"{clean_name(w.stem)}.wav"
                if dest.exists():
                    dest.unlink()          # same title again -> refresh, don't pile up copies
                shutil.move(str(w), dest)
                print(f"   ok  {dest.name}  ({dest.stat().st_size / 1e6:.0f} MB)")
                new.append(dest)
    return new


# ------------------------------------------------------------------- SD card
def removable_drives():
    """Windows removable drives (SD card readers / USB sticks) that are ready."""
    if os.name != "nt":
        return []
    import ctypes
    mask = ctypes.windll.kernel32.GetLogicalDrives()
    found = []
    for i in range(26):
        if mask >> i & 1:
            root = f"{chr(65 + i)}:\\"
            if ctypes.windll.kernel32.GetDriveTypeW(root) == 2 and os.path.exists(root):
                found.append(root)
    return found


def pick_sd(cli_sd=None):
    cfg = load_config()
    if cli_sd:
        p = Path(cli_sd if len(cli_sd) > 2 else cli_sd.rstrip(":\\/") + ":\\")
        if not p.exists():
            sys.exit(f"{p} not found")
        cfg["sd"] = str(p)
        save_config(cfg)
        return p
    drives = removable_drives()
    if len(drives) == 1:
        return Path(drives[0])
    if len(drives) > 1:
        saved = cfg.get("sd")
        if saved in drives:
            return Path(saved)
        for n, d in enumerate(drives, 1):
            print(f"  {n}) {d}  ({shutil.disk_usage(d).total / 1e9:.0f} GB)")
        c = input("Which one is the CYD's SD card? number: ").strip()
        if c.isdigit() and 1 <= int(c) <= len(drives):
            cfg["sd"] = drives[int(c) - 1]
            save_config(cfg)
            return Path(cfg["sd"])
    return None


def sync_to_sd(sd):
    lib = sorted(LIBRARY.glob("*.wav"), key=lambda p: p.name.lower())
    on_card = {p.name.lower(): p for p in sd.glob("*.wav")}
    todo = [p for p in lib
            if p.name.lower() not in on_card or on_card[p.name.lower()].stat().st_size != p.stat().st_size]
    if not todo:
        print(f"SD card {sd} is already up to date ({len(on_card)} songs).")
        return
    print(f"\nCopying {len(todo)} song(s) to {sd} ...")
    copied = 0
    for p in todo:
        free = shutil.disk_usage(sd).free
        if p.stat().st_size + 1_000_000 > free:
            print(f"   !! SD card full - stopped at {p.name}")
            break
        shutil.copyfile(p, sd / p.name)
        copied += 1
        print(f"   -> {p.name}")
    total = len(list(sd.glob("*.wav")))
    print(f"Done. {copied} copied, {total} songs on the card.")
    if total > MAX_TRACKS:
        print(f"!! The player only lists the first {MAX_TRACKS} songs; delete some from the card.")
    print("Eject the card from Windows (system tray > Safely Remove) before pulling it out.")


# ---------------------------------------------------------------------- main
def read_items():
    print("Paste YouTube links, playlist links, or song names (one per line).")
    print("Drag audio files in here too. Empty line to start, or just Enter to only copy to SD.\n")
    items = []
    while True:
        try:
            line = input("> ").strip()
        except EOFError:
            break
        if not line:
            break
        # a .txt file full of links is also fine
        p = Path(line.strip('"'))
        if p.suffix.lower() == ".txt" and p.is_file():
            items += [l.strip() for l in p.read_text(errors="ignore").splitlines()
                      if l.strip() and not l.startswith("#")]
        else:
            items.append(line)
    return items


def expand_args(args):
    """Folders dropped on the .bat -> every audio file inside."""
    out = []
    for a in args:
        p = Path(a.strip('"'))
        if p.is_dir():
            out += [str(f) for f in sorted(p.iterdir()) if f.suffix.lower() in AUDIO_EXT]
        else:
            out.append(a)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("items", nargs="*", help="YouTube links, search text, or audio files")
    ap.add_argument("--sd", help="SD card drive, e.g. E:  (remembered)")
    ap.add_argument("--sync", action="store_true", help="only copy library to the SD card")
    ap.add_argument("--no-sd", action="store_true", help="download only, don't copy to a card")
    ap.add_argument("--update", action="store_true", help="update yt-dlp")
    a = ap.parse_args()

    if a.update:
        sys.exit(subprocess.call([sys.executable, "-m", "pip", "install", "-U", "yt-dlp", "imageio-ffmpeg"]))

    print(f"Library folder: {LIBRARY}\n")
    if not a.sync:
        items = expand_args(a.items) or read_items()
        if items:
            process(items, find_ffmpeg())
    if a.no_sd:
        return
    sd = pick_sd(a.sd)
    if sd is None:
        print("\nNo SD card found. Insert it and run again (just press Enter at the prompt);")
        print("your songs are saved in the library folder and will be copied next time.")
        return
    sync_to_sd(sd)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nCancelled.")
