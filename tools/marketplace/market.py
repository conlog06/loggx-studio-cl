#!/usr/bin/env python3
"""LXStudio-Vorlagen-Marktplatz ohne eigenen Server.

Pakete (.lxpack = ZIP, unkomprimiert) liegen in marketplace/packs/. Die
GitHub-Action veröffentlicht sie samt index.json und Vorschaubildern im
Release „marktplatz“; die App lädt den Index von dort.

  market.py make <vorlagenordner> <ziel.lxpack> --name … [--description …] [--author …]
  market.py check <paket.zip|.lxpack> [--out normalisiert.lxpack]
  market.py index <packs-ordner> <ausgabe-ordner>
"""
import argparse
import datetime
import hashlib
import io
import json
import os
import re
import sys
import zipfile

MAX_BYTES = 100 * 1024 * 1024
ALLOWED_EXT = {
    ".loggx", ".json", ".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".tif", ".tiff", ".heic",
    ".mp4", ".mov", ".mkv", ".webm", ".avi", ".m4v", ".mts",
    ".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".aif", ".aiff", ".opus",
    ".mid", ".midi", ".cube", ".ttf", ".otf", ".srt", ".txt",
}


def slug(text):
    s = text.lower()
    for a, b in (("ä", "ae"), ("ö", "oe"), ("ü", "ue"), ("ß", "ss")):
        s = s.replace(a, b)
    s = re.sub(r"[^a-z0-9]+", "-", s).strip("-")
    return s[:48] or "vorlage"


def preview_png(manifest, project):
    """Schlichtes Vorschaubild: Name und Format auf dunklem Verlauf."""
    from PIL import Image, ImageDraw, ImageFont

    v = project.get("video", {})
    w, h = int(v.get("width", 1920)), int(v.get("height", 1080))
    pw = 480
    ph = max(2, round(pw * h / max(1, w)))
    if ph > 480:
        ph, pw = 480, max(2, round(480 * w / max(1, h)))
    img = Image.new("RGB", (pw, ph))
    d = ImageDraw.Draw(img)
    music = project.get("mode") == "music"
    c0, c1 = ((40, 20, 60), (200, 90, 40)) if music else ((15, 25, 55), (40, 110, 220))
    for y in range(ph):
        t = y / max(1, ph - 1)
        d.line([(0, y), (pw, y)], fill=tuple(int(c0[i] + (c1[i] - c0[i]) * t) for i in range(3)))

    def font(size):
        for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "DejaVuSans-Bold.ttf"):
            try:
                return ImageFont.truetype(f, size)
            except OSError:
                pass
        return ImageFont.load_default()

    name = manifest.get("name", "Vorlage")
    size = max(14, pw // 14)
    f = font(size)
    while d.textlength(name, font=f) > pw * 0.9 and size > 10:
        size -= 2
        f = font(size)
    d.text((pw / 2, ph / 2), name, font=f, fill=(255, 255, 255), anchor="mm")
    d.text((pw / 2, ph / 2 + size), manifest.get("format", ""), font=font(max(10, size // 2)), fill=(220, 220, 230), anchor="mt")
    buf = io.BytesIO()
    img.save(buf, "PNG")
    return buf.getvalue()


def project_format(project):
    if project.get("mode") == "music":
        return "%g BPM" % float(project.get("music", {}).get("bpm", 120))
    v = project.get("video", {})
    return "%d×%d" % (int(v.get("width", 1920)), int(v.get("height", 1080)))


def write_stored(path, entries):
    """ZIP mit Methode 0 – so liest LXStudio Pakete."""
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as z:
        for name, data in entries:
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.flag_bits |= 0x800
            z.writestr(info, data)


def read_entries(path):
    """Liest ein Paket (auch komprimiert), prüft Pfade/Größe, entfernt einen gemeinsamen Oberordner."""
    if os.path.getsize(path) > MAX_BYTES:
        raise ValueError("Paket größer als 100 MB")
    with zipfile.ZipFile(path) as z:
        files = [i for i in z.infolist() if not i.is_dir() and not i.filename.startswith("__MACOSX/")
                 and not os.path.basename(i.filename).startswith(".")]
        total = sum(i.file_size for i in files)
        if total > MAX_BYTES:
            raise ValueError("Inhalt größer als 100 MB")
        names = [i.filename.replace("\\", "/") for i in files]
        for n in names:
            if n.startswith("/") or ".." in n.split("/") or ":" in n:
                raise ValueError("Ungültiger Pfad: " + n)
        # Gemeinsame Oberordner abschneiden (wenn jemand den Ordner gezippt hat)
        prefix = ""
        pl = [n for n in names if n.endswith("project.loggx") and (n == "project.loggx" or n.endswith("/project.loggx"))]
        if len(pl) == 1 and pl[0] != "project.loggx":
            prefix = pl[0][: -len("project.loggx")]
            if not all(n.startswith(prefix) for n in names):
                raise ValueError("Paket enthält Dateien außerhalb des Vorlagenordners")
        out = []
        for info, n in zip(files, names):
            n = n[len(prefix):]
            ext = os.path.splitext(n)[1].lower()
            if ext not in ALLOWED_EXT:
                raise ValueError("Dateityp nicht erlaubt: " + n)
            out.append((n, z.read(info)))
    names = {n for n, _ in out}
    if "project.loggx" not in names:
        raise ValueError("project.loggx fehlt")
    project = json.loads(dict(out)["project.loggx"].decode("utf-8"))
    if project.get("format") != "loggx":
        raise ValueError("Kein LXStudio-Projekt")
    manifest = {}
    if "manifest.json" in names:
        manifest = json.loads(dict(out)["manifest.json"].decode("utf-8"))
    return out, project, manifest


def normalize(src, dst, name=None, description=None, author=None):
    entries, project, manifest = read_entries(src)
    if name:
        manifest["name"] = name
    if description is not None:
        manifest["description"] = description
    if author is not None:
        manifest["author"] = author
    manifest.setdefault("name", project.get("name") or "Vorlage")
    manifest["music"] = project.get("mode") == "music"
    manifest["format"] = project_format(project)
    manifest["app"] = "LXStudio"
    manifest.setdefault("version", 1)
    for k in ("name", "description", "author"):
        manifest[k] = str(manifest.get(k, ""))[: 80 if k != "description" else 400].strip()
    entries = [(n, d) for n, d in entries if n != "manifest.json"]
    if not any(n == "preview.png" for n, _ in entries):
        entries.append(("preview.png", preview_png(manifest, project)))
    entries.append(("manifest.json", json.dumps(manifest, ensure_ascii=False, indent=1).encode("utf-8")))
    entries.sort(key=lambda e: (e[0] != "project.loggx", e[0]))
    write_stored(dst, entries)
    return manifest


def cmd_make(a):
    entries = []
    for root, _, files in os.walk(a.dir):
        for f in files:
            p = os.path.join(root, f)
            entries.append((os.path.relpath(p, a.dir).replace(os.sep, "/"), open(p, "rb").read()))
    tmp = a.out + ".tmp"
    write_stored(tmp, entries)
    m = normalize(tmp, a.out, a.name, a.description, a.author)
    os.remove(tmp)
    print("ok", a.out, m["name"], m["format"])


def cmd_check(a):
    try:
        if a.out:
            m = normalize(a.file, a.out, a.name, a.description, a.author)
        else:
            _, project, m = read_entries(a.file)
            m.setdefault("name", project.get("name", ""))
    except (ValueError, zipfile.BadZipFile, json.JSONDecodeError, UnicodeDecodeError, KeyError) as e:
        print("FEHLER:", e)
        sys.exit(1)
    print(json.dumps(m, ensure_ascii=False))


def cmd_index(a):
    os.makedirs(a.out, exist_ok=True)
    items = []
    old = {}
    if os.path.exists(os.path.join(a.packs, "added.json")):
        old = json.load(open(os.path.join(a.packs, "added.json")))
    for f in sorted(os.listdir(a.packs)):
        if not f.endswith(".lxpack"):
            continue
        path = os.path.join(a.packs, f)
        entries, project, manifest = read_entries(path)
        data = open(path, "rb").read()
        pid = f[: -len(".lxpack")]
        preview = dict(entries).get("preview.png")
        if preview:
            open(os.path.join(a.out, pid + ".png"), "wb").write(preview)
        open(os.path.join(a.out, f), "wb").write(data)
        items.append({
            "id": pid,
            "name": manifest.get("name", pid),
            "description": manifest.get("description", ""),
            "author": manifest.get("author", ""),
            "format": manifest.get("format", project_format(project)),
            "music": project.get("mode") == "music",
            "file": f,
            "preview": pid + ".png" if preview else "",
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "added": old.get(pid, datetime.date.today().isoformat()),
        })
    items.sort(key=lambda i: i["added"], reverse=True)
    index = {"version": 1, "updated": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"), "items": items}
    json.dump(index, open(os.path.join(a.out, "index.json"), "w"), ensure_ascii=False, indent=1)
    print(len(items), "Vorlagen im Index")


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("make", "check"):
        s = sub.add_parser(name)
        if name == "make":
            s.add_argument("dir")
            s.add_argument("out")
        else:
            s.add_argument("file")
            s.add_argument("--out")
        s.add_argument("--name")
        s.add_argument("--description")
        s.add_argument("--author")
    s = sub.add_parser("index")
    s.add_argument("packs")
    s.add_argument("out")
    a = p.parse_args()
    {"make": cmd_make, "check": cmd_check, "index": cmd_index}[a.cmd](a)


if __name__ == "__main__":
    main()
