from pathlib import Path
import importlib.util,hashlib,json
r=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('native_motion',r/'tests/test_tracking_native_math.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.NativeMathTests.setUpClass()
def old(rows):
    position=velocity=0.;time=0.;ready=False;out=[]
    for p,now in rows:
        v=0;dt=now-time
        if ready and .005<=dt<=.5:
            raw=(p-position)/dt
            if abs(raw)<=6000:
                a=dt/(dt+.08);v=raw*a+velocity*(1-a)
        ready=True;position=p;velocity=v;time=now;out.append(v)
    return out
try:
    test=m.NativeMathTests();results=[]
    for hz in (30,60,120):
        rows=[(600*i/hz,1+i/hz) for i in range(20)]
        before=old(rows);after=[v[0] for v in test.motion_sequence([[p,0,0,t] for p,t in rows])]
        results.append({'scenario':'constant 6 m/s start','poll_hz':hz,'first_displacement_old_estimate_mps':before[1]/100,'first_displacement_v8_estimate_mps':after[1]/100})
    rows=[(10*i,1+i/60) for i in range(20)]+[(180,1+20/60)]
    results.append({'scenario':'reverse from +6 to -6 m/s at 60 Hz','old_first_reverse_mps':old(rows)[-1]/100,'v8_first_reverse_mps':test.motion_sequence([[p,0,0,t] for p,t in rows])[-1][0]/100})
    rows=[(10*i,1+i/60) for i in range(20)]+[(190,1+i/60) for i in range(20,25)]
    results.append({'scenario':'stop after 6 m/s movement, 83 ms later','old_estimate_mps':old(rows)[-1]/100,'v8_estimate_mps':test.motion_sequence([[p,0,0,t] for p,t in rows])[-1][0]/100})
    report={'scope':'Synthetic motion vectors: old v7 formula reference vs actual NDK-compiled v8 helper under Unicorn. NOT game hits/FPS or measured weapon ballistics.',
        'previous_source_sha256':'f4d329a7071faec5cb5907e8e929c5421b8b721cc1742130f4e4508c88387093','results':results,'device_run':False}
    (r/'motion-regression-v8.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report,indent=2))
finally:m.NativeMathTests.tearDownClass()
