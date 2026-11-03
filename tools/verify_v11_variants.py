"""Verify compiled variants, not just source-level macros. No device operations."""
import hashlib,json,os,pathlib,re,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
ndk=pathlib.Path(os.environ['ANDROID_NDK_HOME']);host='windows-x86_64' if os.name=='nt' else 'linux-x86_64'
nm=ndk/'toolchains/llvm/prebuilt'/host/'bin'/('llvm-nm.exe' if os.name=='nt' else 'llvm-nm')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
profile=(R/'jni/include/rebootnova/tracking_profile.h').read_text(encoding='utf-8')
probe=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',re.search(r' probe = \{([^}]+)',profile)[1]))
# The original game-function fingerprint is shared with other modules and may
# compile into comparison immediates; it is NOT a reliable exclusion discriminator.
results={'variants':{},'tracking_core_unchanged':{},'scope':'ELF symbols, byte sequences and config/UI text; not live stability validation'}
for f,want in json.loads((R/'tests/tracking_core_baseline.json').read_text()).items():
 p=R/pathlib.PureWindowsPath(f);got=sha(p);assert got==want,f;results['tracking_core_unchanged'][p.relative_to(R).as_posix()]=got
for v,present in [('official',False),('test',True)]:
 binary=R/f'libs-{v}/arm64-v8a/REBOOT-ESP';debug=R/f'obj-{v}/local/arm64-v8a/REBOOT-ESP';b=binary.read_bytes()
 symbols=subprocess.check_output([str(nm),'-C','--defined-only',str(debug)],text=True,encoding='utf-8')
 checks={'manager_symbols':'rebootnova::TrackingManager::' in symbols,'tracking_ui_symbol':'Ui::tracking_detail()' in symbols,'config_keys':b'tracking_' in b,'tracking_title':'子弹追踪'.encode() in b,'tracking_payload':probe in b}
 for name,actual in checks.items():assert actual==present,(v,name,actual)
 for field in [b'aim_converter_enabled=',b'aim_converter_device=',b'overlay_scale_percent=',b'overlay_refresh_vote=']:assert field in b,(v,field)
 results['variants'][v]={'binary':binary.relative_to(R).as_posix(),'sha256':sha(binary),'bytes':len(b),'tracking_presence_checks':checks}
(R/'build/v11-variants.json').write_text(json.dumps(results,indent=2),encoding='utf-8');print(json.dumps(results,indent=2))
