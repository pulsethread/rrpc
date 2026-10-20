"""Embed open runtime support and disc identification metadata, never disc data."""
import argparse
import json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--framework',type=Path,required=True);p.add_argument('--config',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--deps',type=Path);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
config=a.config.read_text(encoding='utf-8').replace('../data/disc/SCES_006.50','game/SCES_006.50').replace('../../Source/Rage Racer (Europe).cue','disc/game.cue')
(a.output/'game.toml').write_text(config,encoding='utf-8')
files={'game.toml':a.output/'game.toml','bios/openbios.bin':a.framework/'bios/openbios.bin','licenses/OpenBIOS.LICENSE':a.framework/'bios/OpenBIOS.LICENSE','assets/psxrecomp.png':a.framework/'assets/psxrecomp.png','licenses/psxrecomp/LICENSE':a.framework/'LICENSE'}
for base in [a.framework/'third_party',a.framework/'recompiler/lib',a.framework/'lib']+([a.deps] if a.deps else []):
 if not base.exists():continue
 for f in sorted(base.rglob('*')):
  if f.is_file() and f.name.lower().startswith(('license','copying')) and f.stat().st_size<1024*1024 and '.git' not in f.parts:
   files['licenses/'+base.name+'/'+f.relative_to(base).as_posix()]=f
rc=['#include <windows.h>'];header=['#pragma once','struct RageSupportFile {const char* name;int id;};','static const RageSupportFile rage_support_files[]={']
for i,(name,path) in enumerate(sorted(files.items()),100):
 if not path.is_file():raise RuntimeError(f'Missing support file: {path}')
 rc.append(f'{i} RCDATA "{path.resolve().as_posix()}"');header.append('{"'+name+'",'+str(i)+'},')
# Identification metadata only. No patches or disc payloads are embedded.
regions=json.loads((Path(__file__).parent/'regions/profiles.json').read_text(encoding='utf-8'))
profile_header=['#pragma once',
 'struct RageRegionProfile {const char* name;const char* serial;const char* exe_sha;unsigned long long bytes;const char* raw_sha;const char* cooked_sha;};',
 'static const RageRegionProfile rage_region_profiles[]={']
for region in regions['regions']:
 profile_header.append('{'+','.join([json.dumps(region['name']),json.dumps(region['serial']),json.dumps(region['exe_sha256']),str(region['raw_bytes'])+'ull',json.dumps(region['raw_sha256']),json.dumps(region['cooked_sha256'])])+'},')
profile_header.append('};')
(a.output/'region_profiles.h').write_text('\n'.join(profile_header)+'\n',encoding='utf-8')
icon=Path(__file__).resolve().parents[1]/'assets/Game.ico'
if not icon.is_file():raise RuntimeError(f'Missing application icon: {icon}')
if icon.is_file():rc.append(f'1 ICON "{icon.resolve().as_posix()}"')
rc.append('1 24 "'+(a.output/'generator.manifest').resolve().as_posix()+'"')
(a.output/'generator.manifest').write_text('''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"><assemblyIdentity version="1.0.0.0" processorArchitecture="*" name="RageRacer.GenerateData" type="win32"/><dependency><dependentAssembly><assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls" version="6.0.0.0" processorArchitecture="*" publicKeyToken="6595b64144ccf1df" language="*"/></dependentAssembly></dependency><trustInfo xmlns="urn:schemas-microsoft-com:asm.v3"><security><requestedPrivileges><requestedExecutionLevel level="asInvoker" uiAccess="false"/></requestedPrivileges></security></trustInfo></assembly>''',encoding='utf-8')
header+=['};']
(a.output/'support_files.h').write_text('\n'.join(header)+'\n',encoding='utf-8');(a.output/'support_files.rc').write_text('\n'.join(rc)+'\n',encoding='utf-8')
