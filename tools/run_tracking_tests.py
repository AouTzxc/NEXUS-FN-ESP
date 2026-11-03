"""Run source contracts, NDK-compiled math, and the real ARM64 payload tests."""
import importlib.util,json,pathlib,subprocess,sys,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
def main():
    results=[]
    for name in ('test_model_color_discovery_contract','test_model_color_owner_contract','test_v11_contract','test_cpp_contract','test_model_chams_contract','test_performance_contract','test_motion_contract','test_live_pose_contract','test_publication_lock_contract','test_package','test_world_decode'):
        spec=importlib.util.spec_from_file_location(name,ROOT/'tests'/f'{name}.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        count=0
        for key,fn in sorted(vars(module).items()):
            if key.startswith('test_') and callable(fn):fn();count+=1
        results.append({'suite':name,'passed':count})
    for name in ('test_tracking_payload','test_tracking_native_math','test_model_chams_callback','test_model_chams_catalog'):
        import ast
        tree=ast.parse((ROOT/'tests'/f'{name}.py').read_text(encoding='utf-8-sig'))
        count=sum(isinstance(node,ast.FunctionDef) and node.name.startswith('test_') for node in ast.walk(tree))
        subprocess.run([sys.executable,str(ROOT/'tests'/f'{name}.py')],check=True)
        results.append({'suite':name,'passed':count})
    evidence={'time_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'suites':results,
        'total_passed':sum(x['passed'] for x in results),'failed':0,
        'scope':'Source contracts + actual NDK math/payload machine-code emulation; NOT Android live testing',
        'device_tests':{'native_install':False,'cache_coherency':False,'fps':False,'moving_hits':False,'match_switch':False,'game_restart':False}}
    (ROOT/'tracking-tests.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8')
    print(json.dumps(evidence,indent=2))
if __name__=='__main__':main()
