"""Fresh P7 run02 after class-specific Puppet destructor gate correction."""
from runtime_p7 import r
r.RUNS={'host':'P7_HOST_02','client':'P7_CLIENT_02'}
r.GATE_PATH=r.OUT/'PRE_NATIVE_RUN02.json'
r.DLLS={role:r.OUT/f'PRE_NATIVE_RUN02_snapshot/artifacts/{role}/{name}.dll' for role,name in [('host','pse_m11_bootstrap'),('client','pse_native_client58b')]}
if __name__=='__main__':r.main()
