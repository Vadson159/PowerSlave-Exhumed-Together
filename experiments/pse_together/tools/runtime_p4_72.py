"""Fresh P4 rework run. Rebind the accepted DOC51/REA lane to immutable evidence.

Every invocation validates frozen DLL/PDB and copied sources; no b60 artifact is loaded.
"""
import runtime_p4 as r
r.OUT=r.ROOT/'docs/evidence/m1/PSE_TOGETHER_P4_REWORK_72'
r.LAB=r.ROOT/'experiments/pse_together/runtime_p4_72'
r.RUNS={'host':'P4_HOST_02','client':'P4_CLIENT_02'}
r.GATE_PATH=r.OUT/'PRE_NATIVE_RUN02.json'
r.DLLS={role:r.OUT/f'PRE_NATIVE_RUN02_snapshot/artifacts/{role}/{name}.dll' for role,name in [('host','pse_m11_bootstrap'),('client','pse_native_client58b')]}
original_verify=r.verify_artifacts
def verify():
    gate=original_verify()
    for row in gate['source_copies']:
        p=r.inside(r.ROOT/row['path'])
        if r.sha(p)!=row['sha256']:raise ValueError('Frozen source changed: '+str(p))
    return gate
r.verify_artifacts=verify
if __name__=='__main__':r.main()
