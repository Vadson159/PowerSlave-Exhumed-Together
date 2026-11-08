"""P14 binding of accepted P1 launcher/loader. No new injection mechanism.

Use through REA MCP's Python spawn lane, as described in RUNTIME_P1.md.
The client has no proxy and is only an input source for the host experiment.
"""
import runtime_p1 as runtime

runtime.LAB=runtime.ROOT/'experiments/pse_together/runtime_p14'
runtime.OUT=runtime.ROOT/'docs/evidence/m1/PSE_TOGETHER_P14'
runtime.RUNS={'host':'P14_HOST_02','client':'P14_CLIENT_02'}
runtime.GATE_PATH=runtime.OUT/'PRE_NATIVE_03.json'
runtime.DLLS={'host':runtime.ROOT/'b60/dll/pse_dll_host_p14/pse_m11_bootstrap.dll',
              'client':runtime.ROOT/'docs/evidence/m1/PSE_TOGETHER_P1/P1_CLIENT_03/native_artifact/pse_native_client58b.dll'}

if __name__=='__main__':
    runtime.main()
