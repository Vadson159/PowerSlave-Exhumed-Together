"""P7 fresh isolated native gate; accepted REA/DOC51 launch path unchanged."""
import runtime_p4 as r
r.OUT=r.ROOT/'docs/evidence/m1/PSE_TOGETHER_P7'
r.LAB=r.ROOT/'experiments/pse_together/runtime_p7'
r.RUNS={'host':'P7_HOST_01','client':'P7_CLIENT_01'}
r.GATE_PATH=r.OUT/'PRE_NATIVE_RUN01.json'
r.DLLS={role:r.OUT/f'PRE_NATIVE_RUN01_snapshot/artifacts/{role}/{name}.dll' for role,name in [('host','pse_m11_bootstrap'),('client','pse_native_client58b')]}
original_verify=r.verify_artifacts
def verify():
    gate=original_verify()
    for row in gate['source_copies']:
        p=r.inside(r.ROOT/row['path'])
        if r.sha(p)!=row['sha256']:raise ValueError('Frozen source changed: '+str(p))
    return gate
r.verify_artifacts=verify
if __name__=='__main__':r.main()
