"""Generate bound P7 mailbox-only controls; never executes/injects or calls KEX.

REA loads the generated script in the selected, isolated owned process.
"""
from pathlib import Path
import argparse
import json
import importlib


def main():
    p = argparse.ArgumentParser()
    p.add_argument('role', choices=['host', 'client'])
    p.add_argument('name')
    p.add_argument('operation', choices=['fixture', 'probe', 'keys', 'damage', 'background'])
    p.add_argument('arguments', nargs='*', type=int)
    p.add_argument('--binding', choices=['runtime_p7','runtime_p7_02','runtime_p7_03'], default='runtime_p7')
    a = p.parse_args()
    r = importlib.import_module(a.binding).r
    if r.RUNS[a.role] != a.name:
        raise ValueError('Only current fresh P7 run may be addressed')
    run = r.OUT / a.name
    isolation = r.load(run / 'ISOLATION_GATE.json')
    cfg = r.load(run / 'NATIVE_ARTIFACT.json')['config']
    if isolation['status'] != 'PASS' or isolation['pid'] != cfg['pid']:
        raise ValueError('Current isolated native runtime gate required')
    args = a.arguments
    if a.operation == 'fixture' and a.role == 'host' and len(args) == 3:
        export, types = 'PSE_P7_HostFixture', ['uint32', 'int', 'int']
    elif a.operation == 'probe' and not args:
        if a.role == 'host':
            export, types, args = 'PSE_P7_HostFixture', ['uint32', 'int', 'int'], [5, 0, 0]
        else:
            export, types = 'PSE_P7_RequestKeyProbe', []
    elif a.operation == 'keys' and len(args) == 2 and all(n > 0 for n in args):
        export = 'PSE_P7_HostPermitKeys' if a.role == 'host' else 'PSE_P7_PermitKeys'
        types = ['uint64', 'uint64']
        receipt = r.load(r.OUT / ('KEYS_GATE_ACCEPTED_' + a.role.upper() + '.json'))
        if receipt.get('status') != 'PASS' or receipt.get('pid') != cfg['pid'] or receipt.get('probe_serial') != args[0] or receipt.get('review_serial') != args[1]:
            raise ValueError('Actual read-only witness and independent key gate receipt required')
    elif a.operation == 'damage' and a.role == 'client' and len(args) == 2:
        export, types = 'PSE_P7_LocalDamage', ['int', 'int']
    elif a.operation == 'background' and a.role == 'client' and args in [[0], [1]]:
        export, types = 'PSE_P4_EnableBackground', ['uint32']
    else:
        raise ValueError('Invalid role, operation, or scalar argument count')
    label = a.operation + '_' + '_'.join(str(n).replace('-', 'm') for n in args)
    destination = run / ('control_' + label + '_' + r.stamp() + '.js')
    script = 'const c=' + json.dumps(cfg) + ';\n'
    script += "if(Process.id!==c.pid||Process.mainModule.path.toLowerCase()!==c.target.toLowerCase())throw Error('target');\n"
    script += "const d=Process.getModuleByName(c.dll_name);if(d.path.toLowerCase()!==c.dll.toLowerCase())throw Error('DLL path');\n"
    script += 'const value=new NativeFunction(d.getExportByName(' + json.dumps(export) + "),'uint32'," + json.dumps(types) + ",'win64')(..." + json.dumps(args) + ');\n'
    script += 'send({kind:"p7_mailbox_control",export:' + json.dumps(export) + ',args:' + json.dumps(args) + ',result:value,kex_calls:0,atomic_control_only:true});\n'
    destination.write_text(script, encoding='utf-8')
    print(destination)


if __name__ == '__main__':
    main()
