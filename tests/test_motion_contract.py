from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read(p):return (ROOT/p).read_text(encoding='utf-8')
SRC='jni/src/rebootnova/'
INC='jni/include/rebootnova/'

def test_motion_sample_uses_fresh_root_not_esp_snapshot_timestamp():
    s=read(SRC+'tracking_manager.cpp')
    assert 'c.sampled_at=seconds(std::chrono::steady_clock::now())' in s
    assert 'history.motion.update(live.anchor,c.sampled_at)' in s
    assert 'history.root!=c.root_ptr' in s

def test_zero_per_callback_velocity_preserves_hook_without_counter_clock():
    s=read(SRC+'tracking_manager.cpp')
    assert 'c.velocity.x/call_rate_' not in s
    assert 'const double zero_step=0;' in s
    for offset in (8,16,24):assert f'put(motion,{offset},zero_step)' in s
    assert 'std::array<unsigned char,40> motion' in s

def test_prediction_recomputed_after_chosen_identity_validation():
    s=read(SRC+'tracking_manager.cpp')
    assert s.index('identity changed before publish')<s.index('const double root_age=')<s.index('const auto prediction=')<s.index('const Vec3d point{c.point.x')
    assert 'root_age>.1' in s
    assert 'c.angle>angle_limit' in s[s.index('const double root_age='):]

def test_unknown_speed_warning_and_live_diagnostics():
    s=read(SRC+'ui.cpp');h=read(INC+'tracking_manager.h')
    assert 'settings_.tracking_projectile_mps<=0' in s
    for field in ('flight_ms','target_speed_mps','prediction_offset_m','root_age_ms','prediction_ms'):
        assert field in s and field in h

def test_v8_does_not_increase_memory_poll_rate_or_roster():
    s=read(SRC+'tracking_manager.cpp')
    assert 'std::chrono::milliseconds(16)' in s
    assert 'shortlist.size()>5' in s
    assert 'scene_component_velocity' not in s
    assert 's.tracking_filter_teammates' in s
