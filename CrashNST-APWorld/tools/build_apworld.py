"""package the apworld"""

import json
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

root = Path(__file__).resolve().parents[1]
world = root / "worlds" / "crash_nst"
output = root / "dist" / "crash_nst.apworld"
manifest = json.loads((world / "archipelago.json").read_text(encoding="utf-8"))
manifest.update(version=7, compatible_version=7)

output.parent.mkdir(exist_ok=True)
with ZipFile(output, "w", ZIP_DEFLATED, compresslevel=9) as archive:
    for name in ("__init__.py", "data.py", "Options.py", "crates.json"):
        archive.write(world / name, f"crash_nst/{name}")
    archive.writestr("crash_nst/archipelago.json", json.dumps(manifest))

with ZipFile(output) as archive:
    assert archive.testzip() is None
    assert set(archive.namelist()) == {
        "crash_nst/__init__.py",
        "crash_nst/data.py",
        "crash_nst/Options.py",
        "crash_nst/archipelago.json",
        "crash_nst/crates.json",
    }
print(output)
