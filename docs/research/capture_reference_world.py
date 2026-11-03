"""Read-only bounded evidence collection for reference comparison.

Never imports or executes reference code, and only writes beneath the mod root.
"""
from pathlib import Path
import datetime
import hashlib
import json
import zipfile

MOD = Path(__file__).resolve().parents[2]
BASE = MOD.parent
OUT = MOD / "docs" / "evidence"


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as src:
        for chunk in iter(lambda: src.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def capture(group, specs):
    rows = []
    for rel, spans in specs:
        path = BASE / rel
        raw = path.read_bytes()
        text = raw.decode("utf-8-sig", errors="replace")
        lines = text.splitlines()
        excerpts = []
        for start, end in spans:
            end = min(end, len(lines))
            assert 1 <= start <= end, (rel, start, end)
            excerpts.append({"start_line": start, "end_line": end,
                             "text": "\n".join(lines[start - 1:end])})
        rows.append({"path": str(path), "relative_to_reference_root": rel,
                     "sha256": hashlib.sha256(raw).hexdigest(), "size": len(raw),
                     "line_count": len(lines), "encoding": "utf-8-sig, replacement on error",
                     "decode_replacements": text.count("\ufffd"), "excerpts": excerpts})
    evidence = {"evidence_id": "REFWORLD_" + group,
                "captured_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "producer": "capture_reference_world.py / Python standard library",
                "authority": "local-reference-source-and-documentation",
                "status": "VERIFIED", "status_scope": "observed source text and local file metadata only",
                "limitations": ["Not REA Evidence; no game code executed.",
                                "Source and README assertions are not runtime verification.",
                                "No address, ABI, layout, or behavioral equivalence to Exhumed 2022 is established.",
                                "Source snapshot hashes identify files; upstream commit/provenance not authenticated."],
                "files": rows}
    dst = OUT / ("REFWORLD_" + group + ".json")
    dst.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{dst.name}: {len(rows)} files, {sum(len(e['text']) for r in rows for e in r['excerpts'])} excerpt chars")


capture("PROVENANCE", [
    ("SlaveDriver-Engine-master/README.md", [(1, 30)]),
    ("SlaveDriver-Engine-master/LICENSE.txt", [(1, 20)]),
    ("PSEPSX-main/README.md", [(1, 16), (32, 64)]),
    ("PSEPSX-main/LICENSE", [(1, 25)]),
    ("Raze-master/README.md", [(1, 40)]),
    ("NBlood-master/README.md", [(1, 5), (42, 64)]),
    ("PowerslaveEX-master-source-code/README.md", [(1, 31)]),
    ("PowerslaveEX-master-source-code/LICENSE.txt", [(1, 20)]),
])

OLD = "PowerslaveEX-master-source-code/kex3_anubis/source/game/"
capture("EX2015_WORLD", [
    (OLD + "world.cpp", [(462, 490), (644, 738), (1098, 1206), (1291, 1429),
                         (1503, 1606), (1780, 1870)]),
    (OLD + "game.cpp", [(1085, 1118), (1305, 1405), (1425, 1535)]),
    (OLD + "game.h", [(173, 205), (260, 290)]),
    (OLD + "ai.cpp", [(126, 173), (189, 242), (473, 497)]),
    (OLD + "pickup.cpp", [(50, 89), (129, 177), (731, 790)]),
    (OLD + "mover.cpp", [(29, 74)]),
    (OLD + "travelObject.cpp", [(62, 108)]),
    (OLD + "menu.cpp", [(1617, 1649)]),
    (OLD + "overWorld.cpp", [(412, 444), (467, 500)]),
])

SAT = "SlaveDriver-Engine-master/"
capture("SATURN", [
    (SAT + "LEVEL.C", [(1, 72)]),
    (SAT + "GAMESTAT.H", [(4, 67)]),
    (SAT + "OBJECT.H", [(25, 110)]),
    (SAT + "OBJECT.C", [(80, 143), (185, 235)]),
    (SAT + "AI.C", [(31, 57)]),
    (SAT + "AICOMMON.C", [(131, 141), (315, 365)]),
    (SAT + "BUP.C", [(154, 180)]),
])

PS = "PSEPSX-main/"
capture("PSEPSX", [
    (PS + "PSEPSX.py", [(10, 17), (45, 51), (426, 467), (469, 501), (525, 532)]),
    (PS + "Resources/Options.json", [(1, 42)]),
    (PS + "Data/scripts/map/psx/generic_script.txt", [(1, 6)]),
    (PS + "Data/scripts/map/psx/post_processing.txt", [(1, 77)]),
    (PS + "Patches/PSEPSX.diff", [(1, 36), (773, 807), (1213, 1240),
                                (1292, 1340), (1522, 1533), (1660, 1685), (1736, 1755)]),
])

RAZE = "Raze-master/source/games/exhumed/src/"
NB = "NBlood-master/source/exhumed/src/"
capture("BUILD_PORTS", [
    (RAZE + "save.cpp", [(24, 105)]),
    (RAZE + "player.h", [(36, 110)]),
    (RAZE + "player.cpp", [(154, 176), (2071, 2138)]),
    (NB + "player.h", [(33, 93)]),
    (NB + "player.cpp", [(109, 138), (650, 689), (705, 729), (819, 837)]),
    (NB + "save.cpp", [(33, 46), (83, 154)]),
    (NB + "switch.cpp", [(100, 142)]),
])

COOP = BASE / "PowerSlave_EX_-_CO-OP_Edition"
capture("COOP_README", [("PowerSlave_EX_-_CO-OP_Edition/README.txt", [(1, 100)])])
files = []
archives = []
for path in sorted(COOP.rglob("*")):
    if path.is_file():
        files.append({"path": str(path), "relative_path": path.relative_to(COOP).as_posix(),
                      "size": path.stat().st_size, "sha256": digest(path)})
        if path.suffix.lower() == ".kpf":
            with zipfile.ZipFile(path, "r") as z:
                members = [{"name": i.filename, "size": i.file_size,
                            "compressed_size": i.compress_size, "crc32": f"{i.CRC:08x}"}
                           for i in z.infolist()]
                text = [{"name": i.filename,
                         "sha256": hashlib.sha256(z.read(i)).hexdigest(),
                         "text": z.read(i).decode("utf-8", errors="replace")}
                        for i in z.infolist() if i.filename.endswith((".txt", ".cfg", ".json"))
                        and i.file_size <= 65536]
                archives.append({"path": str(path), "sha256": digest(path),
                                 "members": members, "small_text_members": text})
inventory = {"evidence_id": "REFWORLD_COOP_INVENTORY", "authority": "local-artifact-read-only",
             "status": "VERIFIED", "producer": "capture_reference_world.py / zipfile, hashlib",
             "scope": str(COOP), "files": files, "archives": archives,
             "limitations": ["No EXE executed or modified; PE analysis belongs to separate evidence.",
                             "No .cpp/.c/.h files found in the complete supplied directory file inventory; this does not establish absence of upstream sources.",
                             "README gameplay and networking claims not verified by runtime or binary analysis here."]}
(OUT / "REFWORLD_COOP_INVENTORY.json").write_text(json.dumps(inventory, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(f"REFWORLD_COOP_INVENTORY.json: {len(files)} supplied files; {len(archives)} archive")
