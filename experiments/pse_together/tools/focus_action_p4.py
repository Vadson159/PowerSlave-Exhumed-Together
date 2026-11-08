"""Accepted focus fixture immediately followed by an owned UI action; no injection."""
import sys
import runtime_p4 as r
role,name,action=sys.argv[1:4]
args=sys.argv[4:]
r.inputs(['focus',role])
m=r.accepted_runner(role)
m.action(name,action,args)
