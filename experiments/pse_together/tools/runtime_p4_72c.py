"""Fresh P4 final candidate, including long-pause buffer recovery."""
import runtime_p4_72 as x
r=x.r
r.RUNS={'host':'P4_HOST_04','client':'P4_CLIENT_04'}
r.GATE_PATH=r.OUT/'PRE_NATIVE_RUN04.json'
r.DLLS={role:r.OUT/f'PRE_NATIVE_RUN04_snapshot/artifacts/{role}/{name}.dll' for role,name in [('host','pse_m11_bootstrap'),('client','pse_native_client58b')]}
if __name__=='__main__':r.main()
