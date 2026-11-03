"""Read-only capture of selected PowerSlave EX 2015 source excerpts.
Run from the mod root. Only docs/evidence/REF2015_*.json is written.
No binary analysis or application execution is performed.
"""
from pathlib import Path
import hashlib
import json
import datetime

MOD = Path(__file__).resolve().parents[2]
ROOT = Path(r"S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerslaveEX-master-source-code")
SRC = ROOT / "kex3_anubis/source"
GROUPS = {
 "REF2015_PLAYER_LIFECYCLE": {
  "player.h": ("game/player.h", [(18, 221)]),
  "puppet_initialization": ("game/player.cpp", [(24, 71), (1015, 1190), (1652, 1721)]),
  "death_damage": ("game/player.cpp", [(140, 248), (254, 331), (969, 1009)]),
  "inventory_use": ("game/player.cpp", [(1341, 1563)]),
  "controller_allocation": ("game/game.cpp", [(473, 513), (614, 635), (1305, 1345)]),
  "controller_accessors": ("game/game.h", [(120, 380)]),
  "pickup_owner_vs_global": ("game/pickup.cpp", [(130, 170), (501, 535), (578, 616)]),
  "camera": ("renderer/renderView.cpp", [(137, 163)]),
  "renderer_local_skip": ("renderer/renderScene.cpp", [(1440, 1482)]),
  "ai_single_target": ("game/ai.cpp", [(473, 497), (928, 985)]),
 },
 "REF2015_LOOP_ACTORS": {
  "session": ("framework/session.cpp", [(20, 49), (57, 103), (158, 283)]),
  "game_dispatch": ("game/game.cpp", [(519, 566), (572, 613), (761, 861)]),
  "play_lifecycle_tick": ("game/playLoop.cpp", [(178, 391), (397, 415), (472, 516)]),
  "game_objects": ("game/game.cpp", [(1160, 1253)]),
  "object_rtti_spawn": ("framework/object.cpp", [(152, 188)]),
  "object_rtti_macros": ("framework/object.h", [(17, 102)]),
  "game_object": ("game/gameObject.cpp", [(27, 137), (230, 255)]),
  "game_object_fields": ("game/gameObject.h", [(16, 77)]),
  "factory": ("game/actorFactory.cpp", [(23, 122), (169, 213)]),
  "actor_lifecycle": ("game/actor.cpp", [(33, 285), (493, 535), (744, 796)]),
  "actor_fields": ("game/actor.h", [(20, 186)]),
  "world_lifecycle": ("game/world.cpp", [(644, 738)]),
  "map_change": ("game/game.cpp", [(1085, 1118)]),
  "rng": ("math/random.cpp", [(18, 114)]),
 },
 "REF2015_PHYSICS_WEAPONS": {
  "command_fields": ("game/playerCmd.h", [(18, 91)]),
  "commands": ("game/playerCmd.cpp", [(31, 287)]),
  "jump_ground": ("game/player.cpp", [(333, 620)]),
  "water_fly": ("game/player.cpp", [(767, 968)]),
  "cmodel_fields": ("game/cmodel.h", [(20, 214)]),
  "cmodel_actor_collision": ("game/cmodel.cpp", [(573, 699)]),
  "cmodel_move_trace": ("game/cmodel.cpp", [(1393, 1518)]),
  "actor_motion": ("game/actor.cpp", [(576, 738)]),
  "weapon": ("game/pWeapon.cpp", [(26, 126), (175, 320)]),
  "actions_hitscan_ammo": ("game/actionDef.cpp", [(213, 318), (425, 437), (581, 635), (733, 745)]),
  "projectile": ("game/projectile.cpp", [(58, 99), (455, 644)]),
 }
}
def digest(data):
 return hashlib.sha256(data).hexdigest()
def main():
 outdir=MOD / "docs/evidence"
 outdir.mkdir(parents=True,exist_ok=True)
 summary=[]
 for group, mapping in GROUPS.items():
  records=[]
  for name,(relative,spans) in mapping.items():
   path=SRC / relative
   raw=path.read_bytes()
   lines=raw.decode("utf-8",errors="replace").splitlines()
   excerpts=[]
   for start,end in spans:
    end=min(end,len(lines))
    if start>len(lines):
     raise ValueError(f"Out of range: {relative}:{start}")
    excerpt="\n".join(lines[start-1:end])
    excerpts.append({"line_start":start,"line_end_inclusive":end,"text":excerpt,
     "normalized_excerpt_sha256":digest(excerpt.encode("utf-8"))})
   records.append({"id":group+"::"+name, "file_path":str(path),
    "relative_path":"kex3_anubis/source/"+relative,
    "source_sha256":digest(raw),"source_size_bytes":len(raw),
    "source_line_count":len(lines),"excerpts":excerpts,
    "classification":"VERIFIED", "authority":"local-reference-source",
    "target_binary_sha256":None, "target_VA":None,"target_RVA":None,
    "target_file_offset":None,
    "limitations":["Observed reference source only; no build, runtime, or KEX 2022 equivalence established.",
    "No inferred byte layout, calling convention, address, or source-to-binary matching."]})
  readme=ROOT/"README.md"
  data={"schema":"pse-together-source-evidence-v1","evidence_id":group,
   "captured_at_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),
   "source_root":str(ROOT),"source_identity":{"README_sha256":digest(readme.read_bytes()),
    "README_text":readme.read_text(errors="replace")},
   "method":"Python stdlib direct byte hashing and line-bounded UTF-8 source reads; not REA/Ghidra evidence",
   "records":records,
   "limitations":["Reference project named PowerSlave EX 2015 by task; README describes unofficial Kex Engine 3.0 PSX remake.",
    "Repository version/commit identity not recovered; file hashes are the reproducibility boundary.",
    "Any relevance to PowerSlave Exhumed 2022 is INFERRED until independently matched."]}
  dest=outdir/(group+".json")
  if dest.exists():
   existing=json.loads(dest.read_text("utf-8"))
   # Preserve the first observation when exact source digests and ranges agree.
   comparable=lambda d:[(x["id"],x["source_sha256"],[(e["line_start"],e["line_end_inclusive"]) for e in x["excerpts"]]) for x in d["records"]]
   if comparable(existing)==comparable(data):
    summary.append({"evidence_id":group,"status":"reused","records":len(records),"path":str(dest)})
    continue
   raise RuntimeError(f"Source changed; preserve prior evidence and select a new capture ID: {dest}")
  dest.write_text(json.dumps(data,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
  summary.append({"evidence_id":group,"status":"created","records":len(records),"bytes":dest.stat().st_size,"sha256":digest(dest.read_bytes())})
 print(json.dumps(summary,ensure_ascii=False,indent=2))
if __name__=="__main__":
 main()

