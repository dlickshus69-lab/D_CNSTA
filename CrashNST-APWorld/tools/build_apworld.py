"""package the apworld

python build_apworld.py            -> dist/crash_nst.apworld, the game "Crash Bandicoot N. Sane Trilogy" (Stable build)
python build_apworld.py --testing  -> dist/crash_nst_testing.apworld, the game "Crash Bandicoot N. Sane Trilogy (Testing)"
                                      (Testing build): its own folder and game name, so both can be installed side by side;
                                      a YAML picks one with its game: line
"""

import json
import sys
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

root = Path(__file__).resolve().parents[1]
world = root / "worlds" / "crash_nst"
testing = "--testing" in sys.argv[1:]
module = "crash_nst_testing" if testing else "crash_nst"
output = root / "dist" / f"{module}.apworld"
manifest = json.loads((world / "archipelago.json").read_text(encoding="utf-8"))
manifest.update(version=7, compatible_version=7)
game = manifest["game"]
if testing:
    manifest["game"] = game + " (Testing)"

output.parent.mkdir(exist_ok=True)
with ZipFile(output, "w", ZIP_DEFLATED, compresslevel=9) as archive:
    for name in ("__init__.py", "data.py", "Options.py", "crates.json"):
        if testing and name == "data.py":
            text = (world / name).read_text(encoding="utf-8")
            line = f'GAME = "{game}"'
            assert text.count(line) == 1, "data.py's GAME line changed"
            archive.writestr(f"{module}/{name}", text.replace(line, f'GAME = "{game} (Testing)"'))
        else:
            archive.write(world / name, f"{module}/{name}")
    archive.writestr(f"{module}/archipelago.json", json.dumps(manifest))

with ZipFile(output) as archive:
    assert archive.testzip() is None
    assert set(archive.namelist()) == {f"{module}/{n}" for n in ("__init__.py", "data.py", "Options.py", "archipelago.json", "crates.json")}
print(output)
