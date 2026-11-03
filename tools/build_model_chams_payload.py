"""Build/validate the standalone chams callback. Does not contact any device."""
import argparse, hashlib, json, os, pathlib, subprocess
from elftools.elf.elffile import ELFFile
ROOT = pathlib.Path(__file__).resolve().parents[1]
def build(output, ndk, catalog=False):
    output = pathlib.Path(output).resolve(); output.mkdir(parents=True, exist_ok=True)
    host = 'windows-x86_64' if os.name == 'nt' else 'linux-x86_64'
    tools = pathlib.Path(ndk)/'toolchains/llvm/prebuilt'/host/'bin'
    ext = '.exe' if os.name == 'nt' else ''
    clang, objcopy = tools/('clang'+ext), tools/('llvm-objcopy'+ext)
    native = ROOT/'native/model_chams'; elf_path = output/'callback.elf'; binary = output/'callback.bin'
    state_offset,state_size=(0x4000,30080) if catalog else (0x2400,1232)
    source= 'catalog.c' if catalog else 'callback.c'
    assembly= 'catalog_entry.S' if catalog else 'entry.S'
    linker= 'catalog.ld' if catalog else 'payload.ld'
    command = [str(clang), '--target=aarch64-linux-android28', '-O2', '-ffreestanding',
        '-fno-stack-protector', '-fno-math-errno', '-fvisibility=hidden', '-nostdlib',
        '-I', str(ROOT/'jni/include'), '-Wl,-T,'+str(native/linker),
        '-Wl,--build-id=none', '-Wl,-no-pie', str(native/assembly),str(native/source),'-o',str(elf_path)]
    subprocess.run(command,check=True)
    with elf_path.open('rb') as stream:
        elf=ELFFile(stream)
        assert elf['e_machine']=='EM_AARCH64' and elf.elfclass==64
        symbols={s.name:(int(s['st_value']),int(s['st_size'])) for s in elf.get_section_by_name('.symtab').iter_symbols()}
        assert symbols['state']==(state_offset,state_size), symbols['state']
        assert elf['e_entry']==symbols['entry'][0]
        update_name='update_catalog' if catalog else 'update_proxy'
        assert symbols['entry'][0]<symbols[update_name][0]<state_offset
        for section in elf.iter_sections():
            if section['sh_type'] in ('SHT_REL','SHT_RELA'):
                assert section.num_relocations()==0,'Unresolved relocations'
        assert not any(s['st_shndx']=='SHN_UNDEF' and s.name for s in elf.get_section_by_name('.symtab').iter_symbols())
        entry=symbols['entry'][0];update=symbols[update_name][0]
    subprocess.run([str(objcopy),'-O','binary',str(elf_path),str(binary)],check=True)
    blob=binary.read_bytes();assert len(blob)==state_offset+state_size
    assert not any(blob[state_offset:]),'Payload must contain no published target addresses'
    # The exact same shared ABI must compile for the C++ host, not just the C callback.
    probe=output/'abi_probe.cpp'
    abi_header='model_chams_multi_abi.h' if catalog else 'model_chams_abi.h'
    abi_type='MultiState' if catalog else 'State'
    probe.write_text(f'#include "rebootnova/{abi_header}"\nstatic_assert(sizeof(rebootnova::model_chams_native::{abi_type})=={state_size});\n',encoding='utf-8')
    subprocess.run([str(clang),'--target=aarch64-linux-android28','-std=c++20','-x','c++','-I',str(ROOT/'jni/include'),'-c',str(probe),'-o',str(output/'abi_probe.o')],check=True)
    inputs=list(dict.fromkeys([ROOT/'jni/include/rebootnova/model_chams_transform.h',native/'callback.c',native/source,native/assembly,native/linker,ROOT/'jni/include/rebootnova/model_chams_abi.h',ROOT/'jni/include/rebootnova'/abi_header]))
    result={'abi_version':2,'entry_offset':entry,'update_offset':update,'state_offset':state_offset,'state_size':state_size,'catalog':catalog,
        'binary_size':len(blob),'binary_sha256':hashlib.sha256(blob).hexdigest(),
        'inputs':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
        'c_and_cpp_layout_verified':True,'live_execution':False,'manager_integrated':False}
    if catalog:result['entry_second_offset']=symbols['entry_second'][0]
    (output/'manifest.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    return result
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog',action='store_true')
    parser.add_argument('--output',type=pathlib.Path,default=None)
    parser.add_argument('--ndk',default=os.environ.get('ANDROID_NDK_HOME') or os.environ.get('ANDROID_NDK_ROOT'))
    args=parser.parse_args()
    if not args.ndk:parser.error('Set ANDROID_NDK_HOME or --ndk')
    output=args.output or ROOT/('build/model_chams_catalog' if args.catalog else 'build/model_chams')
    print(json.dumps(build(output,args.ndk,args.catalog),indent=2))
