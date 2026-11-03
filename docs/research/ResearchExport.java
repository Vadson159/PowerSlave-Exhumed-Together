// @category PSETogether.Research
// Static research exporter. Reads an imported Ghidra Program, never patches target bytes.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import com.google.gson.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class ResearchExport extends GhidraScript {
  Gson gson = new GsonBuilder().setPrettyPrinting().disableHtmlEscaping().create();
  Path out;
  String va(Address a) { return a == null ? null : (a.isMemoryAddress() ? "0x" + a.toString() : a.getAddressSpace().getName()+":0x"+Long.toUnsignedString(a.getOffset(),16)); }
  void write(String name, Object obj) throws Exception {
    Files.writeString(out.resolve(name), gson.toJson(obj), StandardCharsets.UTF_8);
    println("EVIDENCE_WRITTEN " + name);
  }
  Map<String,Object> base(String operation) {
    Map<String,Object> m = new LinkedHashMap<>();
    m.put("schema", "pse-research-ghidra-v1"); m.put("operation", operation);
    m.put("provider", "direct Ghidra headless; NOT REA provider");
    m.put("ghidra_version", ghidra.framework.Application.getApplicationVersion());
    m.put("captured_utc", java.time.Instant.now().toString());
    m.put("executable_path", currentProgram.getExecutablePath());
    m.put("binary_sha256", currentProgram.getExecutableSHA256());
    m.put("language", currentProgram.getLanguageID().toString());
    m.put("compiler_spec", currentProgram.getCompilerSpec().getCompilerSpecID().toString());
    m.put("image_base", va(currentProgram.getImageBase()));
    m.put("authority", "VERIFIED static observations; semantics/prototypes require interpretation");
    m.put("limitations", List.of("No execution, no behavior verification", "Decompiler output is not original source", "Indirect call/xref coverage may be incomplete", "Ghidra inferred types and signatures are not validated ABI contracts"));
    return m;
  }
  Map<String,Object> ident(Function f) {
    Map<String,Object> m=new LinkedHashMap<>();
    if(f==null) return m;
    m.put("name",f.getName(true)); m.put("entry",va(f.getEntryPoint()));
    m.put("rva", f.getEntryPoint().getAddressSpace().equals(currentProgram.getImageBase().getAddressSpace()) ? "0x"+Long.toHexString(f.getEntryPoint().subtract(currentProgram.getImageBase())) : null);
    m.put("signature_ghidra_inferred", f.getSignature().toString());
    m.put("calling_convention_ghidra", f.getCallingConventionName());
    m.put("external", f.isExternal()); m.put("thunk",f.isThunk());
    return m;
  }
  Map<String,Object> ref(Reference r) {
    Map<String,Object> m=new LinkedHashMap<>();
    m.put("from",va(r.getFromAddress())); m.put("to",va(r.getToAddress()));
    m.put("type",r.getReferenceType().toString()); m.put("operand_index",r.getOperandIndex());
    m.put("source_type",r.getSource().toString());
    m.put("containing_function",ident(getFunctionContaining(r.getFromAddress()))); return m;
  }
  void function(Function f) throws Exception {
    if(f==null) { println("FUNCTION_UNRESOLVED"); return; }
    Map<String,Object> m=base("analyze_function"); m.put("function",ident(f));
    List<Object> ranges=new ArrayList<>();
    for(AddressRange ar:f.getBody().getAddressRanges()) ranges.add(Map.of("start",va(ar.getMinAddress()),"end_inclusive",va(ar.getMaxAddress())));
    m.put("body_ranges",ranges); m.put("body_bytes",f.getBody().getNumAddresses());
    List<Object> incoming=new ArrayList<>();
    ReferenceIterator refs=currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint());
    while(refs.hasNext()) incoming.add(ref(refs.next())); m.put("incoming_entry_references",incoming);
    List<Object> outgoing=new ArrayList<>(); List<String> assembly=new ArrayList<>();
    InstructionIterator ii=currentProgram.getListing().getInstructions(f.getBody(),true);
    while(ii.hasNext()) { Instruction ins=ii.next(); assembly.add(va(ins.getAddress())+": "+ins.toString());
      for(Reference r:ins.getReferencesFrom()) if(!r.getReferenceType().isFlow() || r.getReferenceType().isCall()) outgoing.add(ref(r));
    }
    m.put("assembly",assembly); m.put("outgoing_references",outgoing);
    List<Object> called=new ArrayList<>(); for(Function c:f.getCalledFunctions(monitor)) called.add(ident(c)); m.put("direct_callees",called);
    DecompInterface d=new DecompInterface();
    try { d.openProgram(currentProgram); DecompileResults res=d.decompileFunction(f,120,monitor);
      m.put("decompile_completed",res.decompileCompleted()); m.put("decompile_error",res.getErrorMessage());
      if(res.getDecompiledFunction()!=null) m.put("pseudocode_non_source",res.getDecompiledFunction().getC());
    } finally { d.dispose(); }
    write("G_FUNC_"+f.getEntryPoint().toString()+".json",m);
  }
  void inventory() throws Exception {
    Map<String,Object> m=base("inventory"); List<Object> funcs=new ArrayList<>();
    FunctionIterator fi=currentProgram.getFunctionManager().getFunctions(true); while(fi.hasNext()) funcs.add(ident(fi.next()));
    m.put("functions",funcs); m.put("function_count",funcs.size());
    List<Object> blocks=new ArrayList<>(); for(ghidra.program.model.mem.MemoryBlock b:currentProgram.getMemory().getBlocks())
      blocks.add(Map.of("name",b.getName(),"start",va(b.getStart()),"end_inclusive",va(b.getEnd()),"initialized",b.isInitialized(),"read",b.isRead(),"write",b.isWrite(),"execute",b.isExecute()));
    m.put("blocks",blocks); write("G_INVENTORY.json",m);
    Map<String,Object> sm=base("defined_strings_and_xrefs"); List<Object> strings=new ArrayList<>();
    DataIterator di=currentProgram.getListing().getDefinedData(true);
    while(di.hasNext()) { Data data=di.next(); if(!data.hasStringValue()) continue;
      Map<String,Object> s=new LinkedHashMap<>(); s.put("va",va(data.getAddress())); s.put("rva","0x"+Long.toHexString(data.getAddress().subtract(currentProgram.getImageBase())));
      s.put("value",data.getValue().toString()); s.put("type",data.getDataType().getName()); s.put("length",data.getLength());
      List<Object> xr=new ArrayList<>(); ReferenceIterator ri=currentProgram.getReferenceManager().getReferencesTo(data.getAddress()); while(ri.hasNext()) xr.add(ref(ri.next())); s.put("xrefs",xr); strings.add(s);
    }
    sm.put("strings",strings); sm.put("count",strings.size()); write("G_STRINGS_XREFS.json",sm);
    AddressIterator entries=currentProgram.getSymbolTable().getExternalEntryPointIterator();
    List<String> es=new ArrayList<>(); while(entries.hasNext()) {Address a=entries.next();es.add(va(a)); Function f=getFunctionAt(a);if(f!=null) function(f);}
    write("G_ENTRYPOINTS.json",Map.of("entries",es,"binary_sha256",currentProgram.getExecutableSHA256()));
  }
  public void run() throws Exception {
    String[] args=getScriptArgs(); if(args.length<2) throw new IllegalArgumentException("output-directory inventory|function|refs|memory [selectors]");
    out=Paths.get(args[0]).toAbsolutePath(); Files.createDirectories(out);
    if(args[1].equals("inventory")) {inventory();return;}
    if(args[1].equals("symbols")) {
      Map<String,Object> m=base("selected_symbols");List<Object> rows=new ArrayList<>();
      SymbolIterator si=currentProgram.getSymbolTable().getAllSymbols(true);
      while(si.hasNext()){Symbol s=si.next();if(s.getName(true).contains("kexSlave"))rows.add(Map.of("name",s.getName(true),"address",va(s.getAddress()),"kind",s.getSymbolType().toString(),"source",s.getSource().toString()));}
      m.put("symbols",rows);write("G_SLAVE_SYMBOLS.json",m);
    }
    if(args[1].equals("socialsymbols")) {
      Map<String,Object> m=base("selected_achievement_steam_symbols");List<Object> rows=new ArrayList<>();
      SymbolIterator si=currentProgram.getSymbolTable().getAllSymbols(true);
      while(si.hasNext()){Symbol s=si.next();String n=s.getName(true);if(n.contains("Achievements")||n.contains("UserStats")||n.contains("SteamAPI_Init")||n.contains("SteamAPI_RunCallbacks"))rows.add(Map.of("name",n,"address",va(s.getAddress()),"kind",s.getSymbolType().toString(),"source",s.getSource().toString()));}
      m.put("symbols",rows);write("G_ACHIEVEMENT_SYMBOLS.json",m);
    }
    if(args[1].equals("inspect")) for(int i=2;i<args.length;i++) {
      Address a=toAddr(args[i]);Map<String,Object> m=base("instruction_window");m.put("selected_address",va(a));m.put("function_at",ident(getFunctionAt(a)));m.put("function_containing",ident(getFunctionContaining(a)));
      List<Object> rows=new ArrayList<>();InstructionIterator it=currentProgram.getListing().getInstructions(a,true);
      for(int j=0;j<20 && it.hasNext();j++){Instruction ins=it.next();rows.add(Map.of("va",va(ins.getAddress()),"instruction",ins.toString(),"bytes_hex",java.util.HexFormat.of().formatHex(ins.getBytes())));}
      m.put("instructions",rows);write("G_INSPECT_"+a.toString()+".json",m);
    }
    if(args[1].equals("function")) for(int i=2;i<args.length;i++) { Address a=toAddr(args[i]); Function f=getFunctionAt(a);if(f==null)f=getFunctionContaining(a);function(f); }
    if(args[1].equals("refs")) for(int i=2;i<args.length;i++) {
      Address a=toAddr(args[i]); Map<String,Object> m=base("references_to_address");m.put("address",va(a));
      List<Object> refs=new ArrayList<>(); ReferenceIterator ri=currentProgram.getReferenceManager().getReferencesTo(a);while(ri.hasNext())refs.add(ref(ri.next()));m.put("incoming",refs);
      Data data=getDataAt(a);m.put("defined_data",data==null?null:data.toString());write("G_REFS_"+a.toString()+".json",m);
    }
    if(args[1].equals("memory")) for(int i=2;i<args.length;i++) {
      Address a=toAddr(args[i]);Map<String,Object> m=base("initialized_analysis_memory");m.put("address",va(a));List<Object> rows=new ArrayList<>();
      for(int j=0;j<32;j++){Address p=a.add(j*8);long value=currentProgram.getMemory().getLong(p);Map<String,Object> row=new LinkedHashMap<>();row.put("slot",va(p));row.put("u64_hex","0x"+Long.toUnsignedString(value,16));Address dest=toAddr(value);row.put("function",ident(getFunctionAt(dest)));rows.add(row);}m.put("rows",rows);write("G_MEMORY_"+a.toString()+".json",m);
    }
  }
}
