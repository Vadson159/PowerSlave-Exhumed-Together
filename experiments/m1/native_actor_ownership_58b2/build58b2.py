"""DOC-58B.2 offline build/test gate. Usage: build58b2.py <TAG_SUFFIX>. C++20 /W4 /WX, build tree b582/ on S:."""
from workflow import *
CM='C:/Program Files/CMake/bin/cmake.exe'
NODE='C:/Program Files/nodejs/node.exe'
B58=ROOT/'experiments/m1/native_network_visual_proxy_58b'
MOVE58A=ROOT/'experiments/m1/native_visual_proxy_movement'
def main(sfx):
    results={}
    results['configure']=command([CM,'-S',LAB,'-B',SHORT_BUILD,'-G','Visual Studio 17 2022','-A','x64','-DCMAKE_SYSTEM_VERSION=10.0.26100.0'],'CONFIGURE_'+sfx)
    results['build']=command([CM,'--build',SHORT_BUILD,'--config','Release','--parallel','2','--target','proxy58b_baseline','lifecycle58b1_baseline','lifecycle58b2_contract'],'BUILD_'+sfx)
    rel=SHORT_BUILD/'Release'
    if not rel.exists():rel=SHORT_BUILD
    exe=lambda n:next(SHORT_BUILD.rglob(n+'.exe'))
    results['doc58b_contract']=command([exe('proxy58b_baseline')],'TEST_DOC58B_'+sfx)
    results['doc58b1_lifecycle']=command([exe('lifecycle58b1_baseline')],'TEST_DOC58B1_'+sfx)
    results['doc58b2_lifecycle']=command([exe('lifecycle58b2_contract')],'TEST_DOC58B2_'+sfx)
    results['doc58b_adapter_js']=command([NODE,B58/'offline58b.js'],'TEST_ADAPTER58B_'+sfx,cwd=B58)
    results['doc58a_offline_js']=command([NODE,MOVE58A/'offline_test.js'],'TEST_DOC58A_'+sfx,cwd=MOVE58A)
    print(json.dumps({k:v['exit_code'] for k,v in results.items()}))
if __name__=='__main__':main(sys.argv[1])
