"""Extract bounded script registration evidence from saved Ghidra JSON only.
No Ghidra/REA query, game execution, or binary mutation.
"""
from pathlib import Path
import re
import json
import hashlib
import datetime

MOD=Path(__file__).resolve().parents[2]
EV=MOD/"docs/evidence"
GH=EV/"ghidra_2022"
TARGET="b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d"
IMAGE=0x140000000
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def load(p):return json.loads(p.read_text("utf-8-sig"))
def coord(addr,pe):
 n=int(addr,16);rv=n-IMAGE
 hits=[s for s in pe["sections"] if s["rva"]<=rv<s["rva"]+s["raw_size"]]
 return {"preferred_va":hex(n),"rva":hex(rv),
  "file_offset":hex(hits[0]["raw_pointer"]+rv-hits[0]["rva"]) if len(hits)==1 else None,
  "file_offset_method":"PE_main2022.json section raw_pointer + (RVA - section.rva); bounded to raw_size, not Ghidra source-byte map"}
def extract_function(p,pe):
 d=load(p)
 assert d["binary_sha256"]==TARGET
 text=d["pseudocode_non_source"]
 # Semicolons in these saved pseudocode strings do not occur inside literals.
 # Keep original character offsets to cite exact pseudocode lines.
 last={};hist={};records=[];enums=[];types=[];props=[];namespace={"literal":None,"selector":"UNKNOWN"}
 starts=[0]+[m.end() for m in re.finditer(";",text)]
 for ix,start in enumerate(starts):
  end=starts[ix+1] if ix+1<len(starts) else len(text)
  st=text[start:end]
  for m in re.finditer(r"\b(local_[a-fA-F0-9]+)\s*=\s*([^;\r\n]+)",st):
   val={"local":m.group(1),"rhs":m.group(2).strip(),
    "pseudo_line":text.count("\n",0,start+m.start())+1}
   last[m.group(1)]=val
   hist.setdefault(m.group(1),[]).append(val)
  if "(code **)" not in st or not re.search(r"\+\s+0x[0-9a-f]+\)\)",st):continue
  vm=re.search(r"\+\s+(0x[0-9a-f]+)\)\)",st)
  if not vm:continue
  slot=vm.group(1)
  strings=re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"',st)
  callstart=st.find("(**")
  callline=text.count("\n",0,start+callstart)+1
  endline=text.count("\n",0,end)+1
  call=st[callstart:].strip()
  if slot=="0x168":
   namespace={"literal":strings[0] if strings else None,"selector":call}
   continue
  common={"registration_function":d["function"]["entry"],"evidence_file":p.relative_to(MOD).as_posix(),
   "evidence_sha256":sha(p),"pseudocode_call_line":callline,"pseudocode_call_end_line":endline,
   "observed_vtable_slot":slot,"registration_call_pseudocode":call,
   "classification":"VERIFIED static registration observation",
   "limitations":["Registration code presence does not prove this routine executed or registration succeeded.",
    "Pseudocode lines are decompiler output, not original source or executable ABI."]}
  if slot=="0x100" and len(strings)==2:
   value=re.search(r',\s*(-?(?:0x[0-9a-f]+|\d+))\s*\)\s*;',call)
   enums.append({**common,"enum":strings[0],"symbol":strings[1],
    "numeric_literal":value.group(1) if value else None,
    "value":int(value.group(1),0) if value else None})
   continue
  if slot=="0x98" and len(strings)==1:
   num=re.search(r'"\s*,\s*((?:0x[0-9a-f]+|\d+))\s*,\s*((?:0x[0-9a-f]+|\d+))',call)
   types.append({**common,"type":strings[0],
    "observed_byte_size_argument":int(num.group(1),0) if num else None,
    "observed_flags_argument":num.group(2) if num else None,
    "size_semantics":"Registration argument only; no sizeof/lifetime/constructibility conclusion."})
   continue
  local=re.search(r'&(local_[a-fA-F0-9]+)\b',call)
  if slot not in ["0x50","0xa8"] or not local:
   if len(strings)>=2:
    props.append({**common,"scope":strings[0],"declaration_literal":strings[1],
     "native_member_offset_verified":False})
   continue
  var=local.group(1)
  assignment=last.get(var)
  symbolmatch=re.search(r"\b(FUN|LAB)_([0-9a-fA-F]+)\b",assignment["rhs"]) if assignment else None
  symbol=symbolmatch.group(0) if symbolmatch else None
  callback=hex(int(symbolmatch.group(2),16)) if symbolmatch else None
  assignline=assignment["pseudo_line"] if assignment else callline
  # Do not infer from nearest code assignment to another temporary such as local_78.
  row={**common,"registration_kind":"object_method" if slot=="0xa8" else "global_function",
   "scope_literal":strings[0] if slot=="0xa8" and len(strings)>=2 else None,
   "namespace_selector_observed":namespace,
   "script_declaration_literal":strings[1] if slot=="0xa8" and len(strings)>=2 else strings[0],
   "callback_local_passed_by_address":var,"last_assignment_before_registration":assignment,
   "callback_symbol_kind":symbolmatch.group(1) if symbolmatch else None,
   "callback_symbol":symbol,
   "callback_coordinates":coord(callback,pe) if callback else None,
   "callback_native_prototype_verified":False,"native_calling_convention":"UNKNOWN; numeric registration call-convention argument retained only as raw pseudocode",
   "assignment_history_since_previous_registration":hist.get(var,[]),
   "bounded_pseudocode_snippet":{"line_start":max(1,assignline-2),"line_end_inclusive":endline,
    "text":"\n".join(text.splitlines()[max(0,assignline-3):endline])},
   "callback_reference_sites_in_registration_function":[
    {k:r[k] for k in ["from","to","type","operand_index"] if k in r}
    for r in d["outgoing_references"] if r["to"]==callback] if callback else []}
  if callback is not None:
   row["callback_machine_address_loads_in_registration_function"]=[
    a for a in d["assembly"] if callback in a]
  records.append(row);hist[var]=[]
 return {"function":d["function"],"source_file":p.relative_to(MOD).as_posix(),"source_sha256":sha(p),
  "registration_rows":records,"enum_values":enums,"object_type_registrations":types,"property_calls":props}

def main():
 pe=load(EV/"PE_main2022.json");assert pe["sha256"]==TARGET
 entries=["1401f1880","1401f5040","1401f6a70"]
 if (GH/"G_FUNC_1401f84a0.json").exists():entries.append("1401f84a0")
 functions=[extract_function(GH/f"G_FUNC_{a}.json",pe) for a in entries]
 methods=[x for f in functions for x in f["registration_rows"]]
 for x in methods:
  assert x["callback_symbol"],x
 # Static instruction windows may overlap. Preserve provenance for each exact callback.
 inspected=[]
 raw_windows=[]
 for p in sorted(GH.glob("G_INSPECT_*.json")):
  d=load(p);assert d["binary_sha256"]==TARGET
  origin=d["selected_address"]
  raw_windows.append((p,d))
  inst=d.get("instructions",[])
  # Only bounded contiguous prefix to the first RET/branch/call, at most 8 instructions.
  prefix=[]
  for ins in inst[:8]:
   prefix.append(ins)
   if re.match(r"(RET|JMP|JNZ|JZ|CALL)\b",ins["instruction"]):break
  inspected.append({"selected":coord(origin,pe),"source_file":p.relative_to(MOD).as_posix(),
   "source_sha256":sha(p),"function_at":d.get("function_at"),
   "function_containing":d.get("function_containing"),"bounded_instructions":prefix,
   "registered_declarations":[
    {"scope":x["scope_literal"],"script_declaration":x["script_declaration_literal"],
     "registration_function":x["registration_function"],
     "symbol_kind":x["callback_symbol_kind"]}
    for x in methods if x["callback_coordinates"]["preferred_va"]==origin]})
 corroborated=[]
 for callback in sorted(set(x["callback_coordinates"]["preferred_va"] for x in methods)):
  candidate=None
  for p,d in raw_windows:
   ins=d.get("instructions",[])
   hit=next((i for i,x in enumerate(ins) if x["va"]==callback),None)
   if hit is None:continue
   prefix=[]
   for instruction in ins[hit:hit+8]:
    prefix.append(instruction)
    if re.match(r"(RET|JMP|JNZ|JZ|CALL)\b",instruction["instruction"]):break
   roles=[{"scope":x["scope_literal"],"declaration":x["script_declaration_literal"]} for x in methods if x["callback_coordinates"]["preferred_va"]==callback]
   offset=None
   if prefix:
    match=re.search(r"\[RCX \+ (0x[0-9a-f]+)\]",prefix[0]["instruction"])
    if match:offset=match.group(1)
   record={"callback":coord(callback,pe),"source_file":p.relative_to(MOD).as_posix(),
    "source_sha256":sha(p),"instruction_window_selected_address":d["selected_address"],
    "bounded_instructions":prefix,"object_base_register_observed":"RCX" if offset else None,
    "member_displacement_observed":offset,"registered_declarations":roles,
    "prefix_complete_to_terminator":bool(prefix and re.match(r"(RET|JMP|JNZ|JZ|CALL)\b",prefix[-1]["instruction"])),
    "classification":"VERIFIED instruction/registration pairing; runtime object validity and native type contract UNKNOWN"}
   if candidate is None or len(prefix)>len(candidate["bounded_instructions"]):candidate=record
  if candidate:corroborated.append(candidate)
 byaddr={}
 for x in methods:
  byaddr.setdefault(x["callback_coordinates"]["preferred_va"],[]).append({
   "scope":x["scope_literal"],"declaration":x["script_declaration_literal"],
   "symbol":x["callback_symbol"],"registration_function":x["registration_function"]})
 aliases=[{"callback":coord(k,pe),"registrations":v} for k,v in byaddr.items() if len(v)>1]
 kp=EV/"E010_KPF_SELECTED_TEXT.json";kpf=load(kp)
 spans={
 "scripts/main.txt":[(18,26)],
 "scripts/actorTypes.txt":[(18,31)],
 "defs/actors/misc.txt":[(1,18)],
 "defs/actors/items.txt":[(1,20)],
 "defs/actors/artifacts.txt":[(1,18)],
 "scripts/p_misc.txt":[(74,105),(297,317),(372,419)],
 "scripts/p_pspr.txt":[(174,199)],
 "scripts/p_enemy.txt":[(54,71),(77,95)],
 "scripts/map/KILARENA_script.txt":[(18,26)],
 "scripts/map/SELBUROW_script.txt":[(20,35)],
 "scripts/map/SETARENA_script.txt":[(20,35)],
 "scripts/map/SUNKEN_script.txt":[(21,40)],
 "scripts/map/TOMBEND_script.txt":[(75,88),(136,143),(173,184),(226,241),(258,270)],
 "scripts/map/TOMB_script.txt":[(186,198),(288,295),(310,319),(337,350)]}
 ksnips=[]
 for rec in kpf["records"]:
  if rec["member"] not in spans:continue
  lines=rec["text"].splitlines()
  ksnips.append({"member":rec["member"],"member_sha256":rec["sha256"],
   "snippets":[{"line_start":a,"line_end_inclusive":min(b,len(lines)),
    "text":"\n".join(lines[a-1:min(b,len(lines))])} for a,b in spans[rec["member"]]],
   "classification":"VERIFIED shipped KPF text; runtime execution/reachability UNKNOWN"})
 cast=GH/"G_FUNC_140197ee0.json"
 cd=load(cast)
 castrecord={"coordinates":coord(cd["function"]["entry"],pe),"source_file":cast.relative_to(MOD).as_posix(),
  "source_sha256":sha(cast),"function":cd["function"],
  "pseudocode_non_source":cd["pseudocode_non_source"],
  "assembly":cd["assembly"],
  "classification":"VERIFIED static instructions/decompiler observation; node/cast semantics INFERRED"}
 output={"schema":"pse-script-registration-map-v1","evidence_id":"E014_SCRIPT_REGISTRATION_MAP",
  "captured_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),
  "binary_sha256":TARGET,"preferred_image_base":hex(IMAGE),
  "provider":"Derived extraction from saved direct Ghidra 12.2 evidence, NOT REA",
  "method":"Sequential semicolon-delimited pseudocode statements; LAST assignment of exact local passed by address; explicit FUN/LAB kind; all source evidence hashes retained.",
  "binary_coordinate_source":{"path":"docs/evidence/PE_main2022.json","sha256":sha(EV/"PE_main2022.json"),
   "aslr_dynamic_base_flag":pe["aslr_dynamic_base_flag"]},
  "functions":functions,"inspected_callback_prefixes":inspected,
  "callback_prefixes_from_overlapping_saved_windows":corroborated,"callback_alias_groups":aliases,
  "player_getactor_cast_helper":castrecord,
  "shipped_script_evidence":{"source_file":kp.relative_to(MOD).as_posix(),"source_sha256":sha(kp),
   "kpf_sha256":kpf["zip_sha256"],"records":ksnips},
  "limitations":["No target execution, callback invocation, injection or patching.",
   "Exact script declaration strings are observed; native C++ types/ABI are not recovered signatures.",
   "LAB labels are not silently promoted to Ghidra function entries.",
   "Field offset claims require instruction corroboration; object-type size and property-offset registration arguments alone do not prove layout.",
   "Player.GetActor uses an intervening node/type-resolution helper; +0xd8 must not be treated as raw Puppet pointer.",
   "Callback aliases do not prove ownership, reference counting, safe direct calls or multiplayer support.",
   "Namespace address-only selectors remain unresolved instead of being assumed to mean the global namespace.",
   "KPF code presence does not prove script compilation, invocation, branch execution or final event behavior.",
   "All addresses refer to this exact SHA and preferred image coordinates; runtime module base must be measured."]}
 dest=EV/"E014_SCRIPT_REGISTRATION_MAP.json"
 dest.write_text(json.dumps(output,ensure_ascii=False,indent=2)+"\n","utf-8")
 print(json.dumps({"output":str(dest),"bytes":dest.stat().st_size,"sha256":sha(dest),
  "functions":len(functions),"methods":len(methods),"enum_values":sum(len(f["enum_values"]) for f in functions),
  "alias_groups":len(aliases),"instruction_windows":len(inspected)},indent=2))
if __name__=="__main__":main()

