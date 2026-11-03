from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read(p):return (ROOT/p).read_text(encoding='utf-8')
SRC='jni/src/rebootnova/'
INC='jni/include/rebootnova/'

def test_perf_settings_persist_and_clamp():
    config=read(SRC+'config_store.cpp');settings=read(INC+'settings.h')
    for name in ('performance_preset','entity_refresh_hz','recovery_scan_kb','detail_distance_m','circle_segments','text_shadow','performance_hud'):
        assert name in settings and name in config
    assert 'performance_preset{3}' in settings
    for name in ('performance_preset','entity_refresh_hz','recovery_scan_kb','detail_distance_m','circle_segments'):
        assert 'std::clamp(as_int(values,"'+name+'"' in config
    assert 's.fps_cap = std::clamp' in config

def test_60fps_not_overwritten_on_start():
    main=read(SRC+'main.cpp')
    assert 'settings.fps_cap == 60' not in main
    assert 'settings.fps_cap = 120' not in main

def test_display_queries_throttled_and_not_invalidated_on_query_failure():
    main=read(SRC+'main.cpp')
    assert 'if(frame_start>=next_display_poll)' in main
    assert 'std::chrono::milliseconds(250)' in main
    assert 'if(latest.width>0&&latest.height>0)display=latest;' in main
    assert 'reader.set_profile(profile);' not in main

def test_refresh_local_does_not_make_old_entities_fresh():
    source=read(SRC+'game_reader.cpp')
    refresh=source[source.index('bool GameReader::refresh_local('):source.index('FrameSnapshot GameReader::capture(')]
    assert 'frame.timestamp=' not in refresh and 'frame.timestamp =' not in refresh
    for token in ('frame.pid==memory_.pid()','frame.module_base==memory_.module_base()',
                  'local.world!=frame.local.world','local.pawn!=frame.local.pawn','local.controller!=frame.local.controller',
                  'local.player_state!=frame.local.player_state','performance::reusable(age','frame.local=local;'):
        assert token in refresh

def test_capture_gating_keeps_features_at_render_cadence():
    source=read(SRC+'feature_engine.cpp')
    worker=source[source.index('void FeatureEngine::capture_loop('):source.index('void FeatureEngine::tick(')]
    tick=source[source.index('void FeatureEngine::tick('):source.index('void FeatureEngine::draw(')]
    assert 'capture_gate_.due(now, hz)' in worker
    assert 'snapshot = reader_.capture();' in worker
    assert 'published_frame_ = std::move(snapshot);' in worker
    assert '(frame_.local.valid && !reader_.refresh_local(frame_))' in tick
    assert 'frame_ = published_frame_;' in tick
    assert tick.index('frame_ = published_frame_;') < tick.index('update_aim(screen_width, screen_height);')
    assert tick.index('refresh_local(frame_)') < tick.index('update_aim(screen_width, screen_height);')
    assert tick.index('update_aim(screen_width, screen_height);') < tick.index('tracking_.update(frame_, settings_, reader_.profile());')
    for token in ('update_aim(screen_width, screen_height);','tracking_.update(frame_, settings_, reader_.profile());'):
        assert token in tick
    assert 'render_boxes_.clear();' in tick

def test_recovery_budget_shared_and_mesh_not_starved():
    src=read(SRC+'game_reader.cpp')
    assert 'scan_remaining_=scan_budget_bytes_.load(std::memory_order_relaxed);' in src
    assert 'take_scan_budget(std::min(budget,scan_budget_bytes_.load(std::memory_order_relaxed)/2))' in src
    assert 'take_scan_budget(std::min(kActorColdScanChunk,scan_remaining_/2))' in src
    assert 'take_scan_budget(discovered_meshes_.empty()' in src

def test_live_actor_list_still_authoritative_each_capture():
    src=read(SRC+'game_reader.cpp')
    assert 'read_entities(frame.local, frame.entities, frame.entity_diagnostics);' in src
    assert 'memory_.read_scatter(fingerprint_reads)' in src
    assert 'std::unordered_set<std::uintptr_t> unique_actors' in src

def test_expensive_diagnostic_probes_cached_and_reset():
    src=read(SRC+'game_reader.cpp')
    assert 'const bool diagnostic_due=frame.timestamp>=next_diagnostic_probe_' in src
    assert 'next_diagnostic_probe_=frame.timestamp+std::chrono::milliseconds(500)' in src
    assert src.count('next_diagnostic_probe_={};cached_local_probe_={};cached_verified_probe_={};')==2

def test_render_reuses_precomputed_projection():
    src=read(SRC+'feature_engine.cpp');draw=src[src.index('void FeatureEngine::draw('):]
    assert 'const Projection projection(frame_.local.camera' in draw
    assert 'return projection.project(world,out);' in draw
    assert 'head, bottom, box_width,&projection)' in draw
    assert 'projection?projection->project(bone,screen)' in src

def test_visual_lod_does_not_change_target_candidates():
    src=read(SRC+'feature_engine.cpp')
    assert 'settings_.skeleton && entity.distance_m<=settings_.detail_distance_m' in src
    assert 'settings_.player_platform && entity.distance_m<=settings_.detail_distance_m' in src
    assert 'settings_.text_shadow' in src
    assert 'detail_distance_m' not in read(SRC+'tracking_manager.cpp')
    assert 'detail_distance_m' not in read(SRC+'game_reader.cpp')

def test_perf_ui_uses_saved_preset_and_existing_fine_buttons():
    ui=read(SRC+'ui.cpp');page=ui[ui.index('void Ui::misc_detail()'):]
    for key in ('##perf_preset','##entity_hz','##detail_distance','##circle_quality','##recovery_budget','##performance_stats'):
        assert key in page
    assert 'if(custom){settings_.performance_preset=3;changed=true;}' in page
    assert 'if(changed){std::string error;if(!config_.save(settings_,&error))status_=error;}' in page

def test_timing_brackets_present_and_draw_separately():
    main=read(SRC+'main.cpp')
    assert main.index('const auto draw_start=')<main.index('features.draw(')<main.index('const auto present_start=')<main.index('graphics->EndFrame();')<main.index('const auto present_end=')
    assert 'features.report_performance(frame_seconds,' in main
