"""Run installed Ghidra 12.2 directly; no REA provider or target execution.

Usage: python docs/research/invoke_ghidra.py import
       python docs/research/invoke_ghidra.py function 0x140...
Projects, JVM settings, caches, temporary files and logs stay inside mod.
"""
import argparse, datetime, json, os, pathlib, subprocess

ROOT=pathlib.Path(__file__).resolve().parents[2]
JAVA=pathlib.Path(r'S:\AI-Tools\jdk\jdk-25\bin\java.exe')
GHIDRA=pathlib.Path(r'S:\AI-Tools\ghidra\ghidra_12.2_PUBLIC')
DEFAULT_TARGET=r'S:\Games\SteamLibrary\steamapps\common\PowerSlave Exhumed\anubis_Shipping_Steam_x64.exe'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode',choices=['import','function','refs','memory','symbols','socialsymbols','inspect'])
    p.add_argument('selectors',nargs='*')
    p.add_argument('--project',default='PSE_2022_cpu2')
    p.add_argument('--target',default=DEFAULT_TARGET)
    p.add_argument('--output',default='ghidra_2022')
    a=p.parse_args()
    runtime=ROOT/'docs/research/runtime'
    project=ROOT/'docs/research/ghidra_projects'
    output=ROOT/'docs/evidence'/a.output
    for d in (runtime,project,output): d.mkdir(parents=True,exist_ok=True)
    if not output.resolve().is_relative_to(ROOT): raise ValueError('output outside mod')
    args=[str(JAVA),'-Xmx8G','-XX:ParallelGCThreads=2','-XX:CICompilerCount=2','-Dcpu.core.limit=2',
          '-Djava.system.class.loader=ghidra.GhidraClassLoader','-Dfile.encoding=UTF8',
          '-Dlog4j.skipJansi=true','--enable-native-access=ALL-UNNAMED','--sun-misc-unsafe-memory-access=allow']
    for key,rel in [('user.home',''),('java.io.tmpdir',''),('application.settingsdir','settings'),
                    ('application.cachedir','cache'),('application.tempdir','temp')]:
        args.append('-D'+key+'='+str(runtime/rel))
    args+=['-cp',str(GHIDRA/'Ghidra/Framework/Utility/lib/Utility.jar'),'ghidra.Ghidra',
           'ghidra.app.util.headless.AnalyzeHeadless',str(project),a.project]
    if a.mode=='import': args+=['-import',a.target,'-analysisTimeoutPerFile','1800']
    else: args+=['-process',pathlib.Path(a.target).name,'-noanalysis','-readOnly']
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
    args+=['-scriptPath',str(pathlib.Path(__file__).parent),'-postScript','ResearchExport.java',
           str(output),'inventory' if a.mode=='import' else a.mode]+a.selectors
    args+=['-log',str(output/f'headless_{stamp}.log'),'-scriptlog',str(output/f'script_{stamp}.log')]
    (output/f'invocation_{stamp}.json').write_text(json.dumps({'arguments':args,'utc':stamp,
         'note':'Direct Ghidra fallback; no target process is started'},indent=2),encoding='utf8')
    env=os.environ.copy();env.update(TEMP=str(runtime),TMP=str(runtime),JAVA_HOME=str(JAVA.parent.parent))
    with (output/f'console_{stamp}.log').open('w',encoding='utf8') as log:
        result=subprocess.run(args,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
    (output/f'exit_{stamp}.json').write_text(json.dumps({'exit_code':result.returncode,
          'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}),encoding='utf8')
    print(json.dumps({'exit':result.returncode,'console':str(output/f'console_{stamp}.log')}))
    raise SystemExit(result.returncode)

if __name__=='__main__': main()
