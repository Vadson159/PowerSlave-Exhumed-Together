/* Run ONLY in a disposable Python diagnostic host, never a game.
   Mock memory below belongs entirely to this script; no engine objects/functions/hooks. */
if(!['python.exe','pythonw.exe'].includes(Process.mainModule.name.toLowerCase()))throw new Error('Diagnostic host required');
const zero=n=>{const p=Memory.alloc(n);p.writeByteArray(new Uint8Array(n));return p;};
const primaryMock=zero(0x198),secondaryMock=zero(0x198),actorMock=zero(0x1e0),nodeMock=zero(0x48),gameMock=zero(8),countsMock=zero(8);
secondaryMock.add(0xd8).writePointer(nodeMock);nodeMock.add(0x28).writePointer(actorMock);
actorMock.add(0x10).writePointer(nodeMock);actorMock.add(0x1b8).writePointer(secondaryMock);gameMock.writeU32(3);
const originalMock=new CModule(`
 extern unsigned char counts_mock[];
 extern unsigned char primary_mock[];
 void original_update(void *p) { (*(unsigned int *)(counts_mock+(p==primary_mock?0:4)))++; }
`,{counts_mock:countsMock,primary_mock:primaryMock});
const observedTid=Process.getCurrentThreadId();
const adapterMock=createControllerAdapter(Process.mainModule.base,primaryMock,secondaryMock,
 {simulationTid:observedTid,prefixes:{},gameState:gameMock,skipDeadSecondary:true,preserveLocalInput:true});
adapterMock.bindIdentity(actorMock,nodeMock);
// Test only the adapter-owned native dispatch seam. install() is intentionally never called.
adapterMock.state.writePointer(originalMock.original_update);adapterMock.state.add(40).writeU32(1);
const invokeMock=new NativeFunction(adapterMock.cm.controller,'void',['pointer'],'win64');
const actualCalls=()=>[countsMock.readU32(),countsMock.add(4).readU32()];
const results=[];
function check(name,expected,predicate){const observed={calls:actualCalls(),adapter:adapterMock.counts(),
 inputs:{p1:{bits:primaryMock.readU32(),jump_ticks:primaryMock.add(8).readU32()},p2:{bits:secondaryMock.readU32(),jump_ticks:secondaryMock.add(8).readU32()}}};
 const passed=predicate(observed);results.push({name,expected,observed,passed});if(!passed)throw new Error('Native mock regression: '+name);}
invokeMock(primaryMock);
check('alive_dispatch','P1 and P2 originals once',r=>r.calls[0]===1&&r.calls[1]===1&&r.adapter.p2_controller===1);
actorMock.add(0x1c0).writeU32(0x200);invokeMock(primaryMock);
check('dead_secondary','P1 progresses, P2 original skipped',r=>r.calls[0]===2&&r.calls[1]===1&&r.adapter.p2_dead_skips===1);
actorMock.add(0x1c0).writeU32(0);nodeMock.add(0x28).writePointer(ptr(0));invokeMock(primaryMock);
check('invalid_generation','No P2 native call on mismatched payload',r=>r.calls[0]===3&&r.calls[1]===1&&r.adapter.p2_identity_skips===1);
nodeMock.add(0x28).writePointer(actorMock);adapterMock.state.add(84).writeU32(observedTid+1);invokeMock(primaryMock);
check('other_thread_condition','No synthetic P2 on TID mismatch',r=>r.calls[0]===4&&r.calls[1]===1);
adapterMock.state.add(84).writeU32(observedTid);gameMock.writeU32(2);invokeMock(primaryMock);
check('non_gameplay_state','No synthetic P2 during state transition',r=>r.calls[0]===5&&r.calls[1]===1);
gameMock.writeU32(3);adapterMock.state.add(40).writeU32(0);invokeMock(primaryMock);
check('disabled_dispatch','Original P1 only after scheduling disabled',r=>r.calls[0]===6&&r.calls[1]===1);
adapterMock.state.add(40).writeU32(1);adapterMock.state.add(8).writePointer(originalMock.original_update);
const commandMock=new NativeFunction(adapterMock.cm.commands,'void',['pointer'],'win64');
primaryMock.add(8).writeU32(900);secondaryMock.add(8).writeU32(777);
adapterMock.set(10000,0,2);commandMock(secondaryMock);
check('late_first_press','P2 first press is tick1 regardless of global frame/hardware counter; P1 unchanged',r=>r.inputs.p2.bits===2&&r.inputs.p2.jump_ticks===1&&r.inputs.p1.jump_ticks===900);
secondaryMock.add(8).writeU32(0);adapterMock.set(10001,0,2);commandMock(secondaryMock);
check('held_press_after_hardware_overwrite','P2 held duration survives original hardware-builder overwrite',r=>r.inputs.p2.jump_ticks===2);
adapterMock.set(10002,0,0);commandMock(secondaryMock);
check('released_press','P2 release resets held duration',r=>r.inputs.p2.bits===0&&r.inputs.p2.jump_ticks===0);
adapterMock.set(10050,0,2);commandMock(secondaryMock);
check('second_press','A new press is tick1 again',r=>r.inputs.p2.jump_ticks===1);
commandMock(primaryMock);
check('preserved_primary_input','P1 hardware path leaves its key duration unchanged',r=>r.inputs.p1.jump_ticks===900&&r.inputs.p2.jump_ticks===1);
adapterMock.state.add(80).writeU32(0);adapterMock.set(10051,2,2);commandMock(primaryMock);
check('independent_synthetic_primary','Independent P1 fixture press is tick1 without changing P2 duration',r=>r.inputs.p1.jump_ticks===1&&r.inputs.p2.jump_ticks===1);
send({kind:'M0_native_controller_mock_tests',status:'PASS',tests:results,
 scope:'Actual CModule machine code against adapter-owned mock memory. No game ABI, physics, death or lifetime acceptance.',game_functions_called:false,game_hooks_installed:false});
