from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read(p):return (ROOT/p).read_text(encoding='utf-8')
SRC='jni/src/rebootnova/'
INC='jni/include/rebootnova/'

def test_tracking_final_target_no_longer_uses_esp_bones_or_box():
    s=read(SRC+'tracking_manager.cpp')
    for forbidden in ('e.bones','c.entity->bones','render_boxes_','project_skeletal_bounds','heights[]'):
        assert forbidden not in s
    assert 'c.raw=live.point;' in s
    assert 'history.motion.update(live.anchor,c.sampled_at)' in s

def test_live_sampling_only_after_single_actor_selection():
    s=read(SRC+'tracking_manager.cpp')
    assert s.count('skeletal_target::sample(')==1
    assert s.index('target_lock_.select(')<s.index('skeletal_target::sample(')<s.index('const auto prediction=')
    assert 'return read_bytes(a,out,n);' in s
    assert 'shortlist.size()>5' in s and 'std::chrono::milliseconds(16)' in s

def test_failed_live_sample_cannot_publish_old_point():
    s=read(SRC+'tracking_manager.cpp');start=s.index('if(!live.valid){');end=s.index('// Revalidate actor identity after pose reads')
    guard=s[start:end]
    assert 'return;' in guard and 'target_lock_.reset()' not in guard
    assert 'PublicationAttempt attempt' in s and 'attempt.confirm()' in s
    assert 'motion_.erase(c.entity->actor)' in guard and 'rejected_until_' in guard
    assert s.index('if(!disable())')<start<s.index('write(data_+104,point)')

def test_live_offsets_come_from_existing_profile():
    s=read(SRC+'tracking_manager.cpp')
    for token in ('profile.character_mesh','profile.actor_root_component','profile.mesh_bone_array','profile.mesh_component_to_world','profile.bone_stride'):
        assert token in s
    header=read(INC+'skeletal_target.h')
    assert 'No ESP location/bone/screen-box inputs' in header
    assert 'array_after.data!=array.data' in header and 'mesh_after!=mesh' in header
    assert 'final_point-point).length()>8' in header

def test_reader_accepts_109_bones_and_bounds_each_index():
    s=read(SRC+'game_reader.cpp')
    assert 'array.count >= 111' not in s
    assert 'array.count > 0' in s
    assert 'ids[index] >= static_cast<std::uint16_t>(bone_count)' in s

def test_live_bone_marker_separate_from_prediction_circle():
    s=read(SRC+'feature_engine.cpp')
    assert 'world_to_screen(tracking.raw_point' in s
    assert 'raw.x-4,raw.y' in s and 'raw.x,raw.y-4' in s
    ui=read(SRC+'ui.cpp')
    assert 'Live character bone' in ui and 'Bone missing: height fallback' not in ui
    assert 'd.bone_index,d.bone_count' in ui
