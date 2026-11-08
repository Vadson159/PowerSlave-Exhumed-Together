"""Generate mailbox-only Frida controls; keys require a retained fresh review receipt."""
import argparse,json
from pathlib import Path
from runtime_p16 import r
def main():
    ap=argparse.ArgumentParser();ap.add_argument('role',choices=['host','client']);ap.add_argument('operation',choices=['fixture','probe','keys','restore','background','p7fixture']);ap.add_argument('values',nargs='*',type=int);ap.add_argument('--receipt');a=ap.parse_args()
    run=r.OUT/r.RUNS[a.role];cfg=r.load(run/'NATIVE_ARTIFACT.json')['config'];iso=r.load(run/'ISOLATION_GATE.json')
    if iso['status']!='PASS' or iso['pid']!=cfg['pid'] or r.sha(cfg['dll'])!=cfg['dll_sha256']:raise ValueError('current isolated frozen process required')
    args=a.values
    if a.operation=='fixture' and a.role=='host' and len(args)==1 and 1<=args[0]<=3:export,types='PSE_P16_HostFixture',['uint32']
    elif a.operation=='p7fixture' and a.role=='host' and len(args)==3:export,types='PSE_P7_HostFixture',['uint32','int','int']
    elif a.operation=='probe' and not args:
        export,types,args=('PSE_P7_HostFixture',['uint32','int','int'],[5,0,0]) if a.role=='host' else ('PSE_P7_RequestKeyProbe',[],[])
    elif a.operation in ['keys','restore'] and len(args)==2 and min(args)>0:
        receipt=r.load(a.receipt)
        if receipt['status']!='PASS' or receipt['pid']!=cfg['pid'] or receipt['probe']!=args[0] or receipt['review']!=args[1] or receipt['operation']!=a.operation:raise ValueError('fresh matching native key review required')
        export='PSE_P16_PermitRestore' if a.operation=='restore' else ('PSE_P7_HostPermitKeys' if a.role=='host' else 'PSE_P7_PermitKeys');types=['uint64','uint64']
    elif a.operation=='background' and a.role=='client' and args in [[0],[1]]:export,types='PSE_P4_EnableBackground',['uint32']
    else:raise ValueError('invalid control')
    text='const c='+json.dumps(cfg)+';\n'
    text+="if(Process.id!==c.pid||Process.mainModule.path.toLowerCase()!==c.target.toLowerCase())throw Error('target');\n"
    text+="const d=Process.getModuleByName(c.dll_name);if(d.path.toLowerCase()!==c.dll.toLowerCase())throw Error('DLL');\n"
    text+='const result=new NativeFunction(d.getExportByName('+json.dumps(export)+"),'uint32',"+json.dumps(types)+",'win64')(..."+json.dumps(args)+');\n'
    text+='send({kind:"p16_mailbox",export:'+json.dumps(export)+',args:'+json.dumps(args)+',result,kex_calls:0});\n'
    path=run/('control_'+a.operation+'_'+r.stamp()+'.js');path.write_text(text,encoding='utf8');print(path)
if __name__=='__main__':main()
