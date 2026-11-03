"""Configure/build the DOC58B lab (frozen DOC56 chain + new client DLL + contract test).
Build tree is ROOT/b58: the frozen DOC56 chain nests five 'integration' dirs, which exceeds MSBuild FileTracker MAX_PATH under LAB/build."""
from workflow import ROOT,LAB,CM,command
import sys
BUILD=ROOT/'b58'
if __name__=='__main__':
    command([CM,'-S',LAB,'-B',BUILD,'-G','Visual Studio 17 2022','-A','x64','-DCMAKE_SYSTEM_VERSION=10.0.26100.0'],'CONFIGURE')
    command([CM,'--build',BUILD,'--config','Release','--parallel','2','--target']+(sys.argv[1:] or ['pse_native_client58b','proxy58b_contract']),'BUILD',timeout=1200)
