"""Fresh P7 run03 for stock zero-delay/sound/tactile weapon presentation gate."""
from runtime_p7 import r
r.RUNS={'host':'P7_HOST_03','client':'P7_CLIENT_03'}
r.GATE_PATH=r.OUT/'PRE_NATIVE_RUN03.json'
r.DLLS={role:r.OUT/f'PRE_NATIVE_RUN03_snapshot/artifacts/{role}/{name}.dll' for role,name in [('host','pse_m11_bootstrap'),('client','pse_native_client58b')]}
if __name__=='__main__':r.main()
