"""Package V11 candidate variants, synchronized source, documentation and evidence.
Run after both builds and tools/verify_v11_variants.py.
"""
import argparse,base64,gzip,hashlib,io,json,os,pathlib,re,shutil,subprocess,tarfile
R=pathlib.Path(__file__).resolve().parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--output-dir',required=True);ap.add_argument('--device-evidence');ap.add_argument('--bash');a=ap.parse_args()
 out=pathlib.Path(a.output_dir).resolve();out.mkdir(parents=True,exist_ok=True)
 subprocess.run([os.sys.executable,str(R/'tools/verify_v11_variants.py')],check=True)
 verification=json.loads((R/'build/v11-variants.json').read_text())
 tests=json.loads((R/'tracking-tests.json').read_text());assert tests['failed']==0
 # Only source / declared assets. No build directories, sessions, runtime journals or past binary manifests.
 allowed={'jni','third_party','native','assets','profiles','tests','tools','docs'}
 excluded={'__pycache__','.git','build','obj','libs','obj-official','obj-test','libs-official','libs-test'}
 excluded_names={'atg_hitbox_journal.json','tracking-artifacts.json','tracking-tests.json','model-chams-contract-tests.json','embedded_font.cpp','embedded_font.gz','embedded_font.S'}
 selected=[]
 for f in sorted(R.rglob('*')):
  if not f.is_file() or f.is_symlink():continue
  rel=f.relative_to(R)
  if any(p in excluded for p in rel.parts) or f.name in excluded_names or f.suffix in ('.pyc','.bak'):continue
  if len(rel.parts)>1 and rel.parts[0] not in allowed:continue
  if len(rel.parts)==1 and f.suffix not in ('.md','.json') and f.name not in ('AGENTS.md','LICENSE','NOTICE','.gitignore'):continue
  if '.before-' in f.name or '.bak_' in f.name:continue
  selected.append((rel.as_posix(),f.read_bytes()))
 source_manifest={name:{'sha256':sha(b),'bytes':len(b)} for name,b in selected}
 archive=out/'REBOOT-ESP-CL57566230-v11-dual-source.tar.gz'
 with archive.open('wb') as file:
  with gzip.GzipFile(filename='',mode='wb',fileobj=file,mtime=0) as gz:
   with tarfile.open(fileobj=gz,mode='w') as tar:
    files=selected+[
     ('SOURCE_MANIFEST.json',json.dumps(source_manifest,ensure_ascii=False,indent=2).encode()),
     ('V11_BUILD_MANIFEST.json',json.dumps(verification,indent=2).encode())]
    for name,data in files:
     info=tarfile.TarInfo('reboot-esp-v11/'+name);info.size=len(data);info.mode=0o644;info.mtime=0
     tar.addfile(info,io.BytesIO(data))
 archive_sha=sha(archive.read_bytes())
 # Verify archive without extracting it or following paths/symlinks.
 with tarfile.open(archive,'r:gz') as tar:
  assert len(tar.getmembers())==len(source_manifest)+2
  for name,item in source_manifest.items():assert sha(tar.extractfile('reboot-esp-v11/'+name).read())==item['sha256'],name
 manifest={'release_status':'v11 character coloring live runtime integrated','model_chams_enabled':True,'source_archive_sha256':archive_sha,'variants':{},'tests':tests,'device_evidence':[]}
 ps='powershell.exe' if os.name=='nt' else 'pwsh'
 for v in ['official','test']:
  dst=out/f'REBOOT-ESP-CL57566230-v11-{v}-candidate.sh'
  subprocess.run([ps,'-NoProfile','-ExecutionPolicy','Bypass','-File',str(R/'tools/package-slim.ps1'),'-Variant',v,'-SourceArchivePath',str(archive),'-OutputPath',str(dst)],check=True)
  s=dst.read_text(encoding='utf-8');assert '\r' not in s and s.startswith('#!/system/bin/sh\n')
  assert 'SOURCE_ARCHIVE_SHA256='+archive_sha in s
  blobs={}
  for marker,gz,path in [('REBOOTNOVA_BIN_GZ_B64',True,R/f'libs-{v}/arm64-v8a/REBOOT-ESP')]:
   match=re.search("<<'"+marker+"'[^\\n]*\\n(.*?)\\n"+marker,s,re.S);assert match,marker
   blob=base64.b64decode(match[1],validate=True)
   if gz:blob=gzip.decompress(blob)
   assert blob==path.read_bytes(),marker;blobs[marker]=sha(blob)
  if a.bash:subprocess.run([a.bash,'-n',dst.as_posix()],check=True)
  manifest['variants'][v]={**verification['variants'][v],'sh':dst.name,'sh_sha256':sha(dst.read_bytes()),'embedded':blobs,'shell_syntax_checked':bool(a.bash)}
 # Deliberately mismatched variants MUST fail before writing a package.
 for v,wrong in [('official','test'),('test','official')]:
  dst=out/f'invalid-{v}.sh';assert not dst.exists()
  r=subprocess.run([ps,'-NoProfile','-ExecutionPolicy','Bypass','-File',str(R/'tools/package-slim.ps1'),'-Variant',v,'-BinaryPath',str(R/f'libs-{wrong}/arm64-v8a/REBOOT-ESP'),'-OutputPath',str(dst)],capture_output=True)
  assert r.returncode!=0 and not dst.exists(),v
 manifest['mislabeled_binary_rejection_tests']=2
 if a.device_evidence:
  evidence=pathlib.Path(a.device_evidence)
  for name in ['device-tests.json','render-device-tests.json']:
   b=(evidence/name).read_bytes();shutil.copyfile(evidence/name,out/name);manifest['device_evidence'].append({'file':name,'sha256':sha(b)})
 shutil.copyfile(R/'docs/V11_DUAL_CANDIDATE.md',out/'README.md')
 shutil.copyfile(R/'tools/collect-v11-performance.sh',out/'collect-v11-performance.sh')
 shutil.copyfile(R/'build/v11-variants.json',out/'variant-verification.json')
 (out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
 hashes=[]
 for f in sorted(out.iterdir()):
  if f.is_file() and f.name!='SHA256SUMS.txt':hashes.append(sha(f.read_bytes())+'  '+f.name)
 (out/'SHA256SUMS.txt').write_text('\n'.join(hashes)+'\n',encoding='utf-8')
 print(json.dumps({'output':str(out),'archive_files':len(source_manifest),'source_sha256':archive_sha,'variants':{v:x['sh_sha256'] for v,x in manifest['variants'].items()},'mismatch_rejections':2},indent=2))
if __name__=='__main__':main()
