#!/usr/bin/env python3
from __future__ import annotations
import hashlib, json, pathlib, sys
root = pathlib.Path(__file__).resolve().parents[1]
manifest = root / 'docs' / 'AFTERIMAGE_0_2_CATALOG.json'
asset_dir = root / 'app' / 'flutter' / 'assets' / 'music' / 'afterimage_0_2'
try:
    data = json.loads(manifest.read_text(encoding='utf-8'))
except Exception as exc:
    raise SystemExit(f'CATALOG INVALID: {exc}')
tracks = data.get('tracks')
if not isinstance(tracks, list) or len(tracks) != 12:
    raise SystemExit(f'CATALOG INVALID: expected 12 tracks, got {len(tracks) if isinstance(tracks,list) else "non-list"}')
originals = sum(t.get('kind') == 'original' for t in tracks)
reworks = sum(t.get('kind') == 'instrumental_rework' for t in tracks)
if originals != 7 or reworks != 5:
    raise SystemExit(f'CATALOG INVALID: originals={originals} reworks={reworks}')
seen: set[str] = set()
for t in tracks:
    name = t.get('file')
    if not isinstance(name, str) or not name.endswith('.mp3') or name in seen:
        raise SystemExit(f'CATALOG INVALID filename: {name!r}')
    seen.add(name)
    path = asset_dir / name
    if not path.is_file():
        raise SystemExit(f'CATALOG MISSING: {name}')
    size = path.stat().st_size
    if size != int(t.get('size_bytes', -1)):
        raise SystemExit(f'CATALOG SIZE MISMATCH: {name}: {size} != {t.get("size_bytes")}')
    h = hashlib.sha256()
    with path.open('rb') as fh:
        for block in iter(lambda: fh.read(1024 * 1024), b''):
            h.update(block)
    got = h.hexdigest()
    if got != t.get('sha256'):
        raise SystemExit(f'CATALOG SHA256 MISMATCH: {name}: {got}')
extra = sorted(p.name for p in asset_dir.glob('*.mp3') if p.name not in seen)
if extra:
    raise SystemExit(f'CATALOG UNMANIFESTED MP3: {extra}')
print(f'AFTERIMAGE 0.2 OK originals={originals} reworks={reworks} tracks={len(tracks)} owner={data.get("owner")}')
