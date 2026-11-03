// @category PSETogether.Research
// Narrow read-only operand audit; no analysis/import and no bytes changed.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import com.google.gson.*;
import java.nio.file.*;
import java.util.*;

public class BackgroundFields extends GhidraScript {
  public void run() throws Exception {
    List<Object> rows = new ArrayList<>();
    InstructionIterator it = currentProgram.getListing().getInstructions(true);
    long count=0;
    while(it.hasNext()) {
      Instruction i=it.next(); ++count;
      String t=i.toString();
      if(t.contains(" + 0x319]") || t.contains(" + 0x20c]")) {
        Function f=getFunctionContaining(i.getAddress());
        rows.add(Map.of("va", "0x"+i.getAddress(), "instruction", t,
          "bytes_hex", java.util.HexFormat.of().formatHex(i.getBytes()),
          "containing_function", f==null ? "UNKNOWN" : "0x"+f.getEntryPoint()));
      }
    }
    Map<String,Object> o=new LinkedHashMap<>();
    o.put("binary_sha256",currentProgram.getExecutableSHA256());
    o.put("provider","direct Ghidra 12.2 saved project -noanalysis -readOnly");
    o.put("limitations",List.of("Operand text search over defined instructions only", "Same displacement in other object classes does not imply GameInstance", "No runtime behavior established"));
    o.put("defined_instructions_examined",count); o.put("rows",rows);
    Files.writeString(Paths.get(getScriptArgs()[0]),new GsonBuilder().setPrettyPrinting().create().toJson(o));
    println("BACKGROUND_FIELD_AUDIT "+rows.size());
  }
}
