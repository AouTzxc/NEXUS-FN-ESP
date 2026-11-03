from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read(p):return (ROOT/p).read_text(encoding='utf-8')
def test_publication_guard_covers_all_post_selection_early_returns():
    s=read('jni/src/rebootnova/tracking_manager.cpp')
    start=s.index('PublicationAttempt attempt')
    end=s.index('attempt.confirm()')
    for token in ('identity changed before publish','publication too old','if(!live.valid)',
                  'identity changed after live pose','prediction expired/out of range',
                  'prediction outside angle','sequence write failed','read(data_+16,reference)',
                  'publication failed'):
        assert token in s[start:end],token
    assert s.index('if(!disable())') < s.index('target_lock_.select(') < start
    assert 'target_lock_.reset()' not in s[start:end]
    assert s.index('write(data_+28,budget)',start)<end<s.index('diag_.locked=true',start)
def test_production_uses_publication_lock_and_configured_delay():
    assert 'tracking_selection::PublicationLock target_lock_' in read('jni/include/rebootnova/tracking_manager.h')
    s=read('jni/src/rebootnova/tracking_manager.cpp')
    assert 's.tracking_switch_delay_ms)' in s
    assert '[]{return seconds(std::chrono::steady_clock::now());}' in s
    h=read('jni/include/rebootnova/tracking_selection.h')
    assert '~PublicationAttempt(){if(!committed)lock.miss(clock());}' in h
    assert 'if(!missing){missing=true;failed_since=now;}' in h
    assert 'missing=false;failed_since=0;' in h
    assert 'blocked_index=index' in h

def test_switch_delay_default_persistence_bounds_and_precise_ui():
    assert 'int tracking_switch_delay_ms{750}' in read('jni/include/rebootnova/settings.h')
    c=read('jni/src/rebootnova/config_store.cpp')
    assert 'version=31' in c and 'out << "tracking_switch_delay_ms="' in c
    assert 'as_int(values,"tracking_switch_delay_ms",s.tracking_switch_delay_ms),100,3000)' in c
    u=read('jni/src/rebootnova/ui.cpp')
    assert 'full_width_slider_int("##tracking_switch_delay"' in u
    assert '&settings_.tracking_switch_delay_ms,100,3000' in u
    assert 'Switch delay after loss (ms)' in u
    p=read('tools/package-slim.ps1')
    assert "'tracking_switch_delay_ms='" in p
