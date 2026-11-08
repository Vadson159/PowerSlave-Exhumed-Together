"""P8 ordinary keyboard/mouse fixture for either verified owned window.

Reuses the accepted foreground checks and input body; changes only the role
selected by key/mouse. No game calls, injection or game-memory accesses.
"""
import sys
import runtime_p8 as run

role=sys.argv[1]
if role not in ('host','client'):raise ValueError('owned role required')
args=sys.argv[2:]
source=run._source
start=source.index('def inputs(args):')
end=source.index('\ndef main():',start)
body=source[start:end]
if args and args[0]=='physical':
    if len(args)==4 and args[3]!='handoff':raise ValueError('only explicit foreground handoff allowed')
    body=body.replace('len(args)!=3','len(args) not in (3,4)')
if role=='client':
    # The wrapper has already selected dynamic RUNS and the same foreground
    # implementation; specialize key/mouse's explicit host selector only.
    old="    source = source.replace(\"RUNS={'host':'N58B_HOST_02','client':'N58B_CLIENT_02'}\", 'RUNS=' + repr(RUNS))"
    body=body.replace(old,old+"\n    source = source.replace(\"focus('host',records)\",\"focus('client',records)\")")
exec(compile(body,__file__,'exec'),run.__dict__)
run.inputs(args)
