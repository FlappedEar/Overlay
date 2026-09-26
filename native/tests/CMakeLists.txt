qt_add_executable(flappedear_gui_session_lock_helper
    GuiSessionLockHelper.cpp
    ../src/app/GuiSessionLock.cpp
)
target_compile_features(flappedear_gui_session_lock_helper PRIVATE cxx_std_20)
target_include_directories(flappedear_gui_session_lock_helper PRIVATE ../src)
target_link_libraries(flappedear_gui_session_lock_helper PRIVATE Qt6::Core)
flappedear_enable_warnings(flappedear_gui_session_lock_helper)

qt_add_executable(flappedear_raw_transport_consumer
    RawTransportConsumer.cpp
)
target_compile_features(flappedear_raw_transport_consumer PRIVATE cxx_std_20)
target_link_libraries(flappedear_raw_transport_consumer PRIVATE Qt6::Core)
flappedear_enable_warnings(flappedear_raw_transport_consumer)

qt_add_executable(flappedear_probe_test_helper ProbeTestHelper.cpp)
target_compile_features(flappedear_probe_test_helper PRIVATE cxx_std_20)
target_link_libraries(flappedear_probe_test_helper PRIVATE Qt6::Core)
flappedear_enable_warnings(flappedear_probe_test_helper)

qt_add_executable(flappedear_native_tests
    ../src/app/AppControllerImport.cpp
    ../src/app/AppControllerComparison.cpp
    ../src/app/AppControllerOuting.cpp
    ../src/app/AppControllerSegmentReview.cpp
    ../src/app/AppControllerCornerAnalyzer.cpp
    ../src/app/AppControllerTheoreticalBest.cpp
    TelemetryTests.cpp
    ../src/app/AppController.cpp
    ../src/app/AppController.h
)
target_compile_features(flappedear_native_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_native_tests PRIVATE flappedear_core Qt6::Concurrent Qt6::Gui Qt6::GuiPrivate Qt6::Multimedia Qt6::QuickControls2 Qt6::Test ZLIB::ZLIBSTATIC)
flappedear_enable_warnings(flappedear_native_tests)
qt_add_resources(flappedear_native_tests "flappedear_test_qml"
    PREFIX "/qt/qml/FlappedEar/qml"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../qml"
    FILES
        ../qml/TelemetryScene.qml
        ../qml/widgets/ArcGaugeWidget.qml
        ../qml/widgets/ComparisonTileWidget.qml
        ../qml/widgets/DialGaugeWidget.qml
        ../qml/widgets/RetroGrandPrixWidget.qml
        ../qml/widgets/RetroSpeedArcWidget.qml
        ../qml/widgets/RetroTachometerWidget.qml
        ../qml/widgets/TelemetryPanel.qml
)
target_compile_definitions(flappedear_native_tests PRIVATE
    GUI_SESSION_LOCK_HELPER_PATH="$<TARGET_FILE:flappedear_gui_session_lock_helper>"
    TEST_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/basic.vbo"
    ANALYSIS_PANEL_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../qml/AnalysisPanel.qml"
    BATCH_IMPORT_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../qml/BatchImportDialog.qml"
    TRACK_WIDGET_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../qml/widgets/TrackWidget.qml"
    RAW_TRANSPORT_CONSUMER_PATH="$<TARGET_FILE:flappedear_raw_transport_consumer>"
    PROBE_TEST_HELPER_PATH="$<TARGET_FILE:flappedear_probe_test_helper>"
    FLAPPEDEAR_NATIVE_PATH="$<TARGET_FILE:flappedear_native>"
)
add_dependencies(flappedear_native_tests flappedear_gui_session_lock_helper flappedear_native flappedear_raw_transport_consumer flappedear_probe_test_helper)
add_test(NAME flappedear_native_tests COMMAND flappedear_native_tests)
# Window interaction tests need real native handles for the QRhi swapchain.
# Offscreen QPA supplies synthetic IDs, not NSView/HWND handles. Preserve the
# renderer/backend and all export pixel assertions; startup smoke is separate.
if(APPLE)
    set_tests_properties(flappedear_native_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=cocoa")
elseif(WIN32)
    set_tests_properties(flappedear_native_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
endif()
qt_add_executable(flappedear_rcz_tests RczTests.cpp RczFixture.h)
target_compile_features(flappedear_rcz_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_rcz_tests PRIVATE flappedear_core Qt6::Test ZLIB::ZLIBSTATIC)
flappedear_enable_warnings(flappedear_rcz_tests)
add_test(NAME flappedear_rcz_tests COMMAND flappedear_rcz_tests)
qt_add_executable(flappedear_import_tests TelemetryImportTests.cpp RczFixture.h)
target_compile_features(flappedear_import_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_import_tests PRIVATE flappedear_core Qt6::Test ZLIB::ZLIBSTATIC)
flappedear_enable_warnings(flappedear_import_tests)
add_test(NAME flappedear_import_tests COMMAND flappedear_import_tests)
qt_add_executable(flappedear_source_cache_tests TelemetrySessionCacheTests.cpp RczFixture.h)
target_compile_features(flappedear_source_cache_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_source_cache_tests PRIVATE flappedear_core Qt6::Test ZLIB::ZLIBSTATIC)
flappedear_enable_warnings(flappedear_source_cache_tests)
add_test(NAME flappedear_source_cache_tests COMMAND flappedear_source_cache_tests)
qt_add_executable(flappedear_event_project_tests EventProjectTests.cpp EventProjectFixture.h)
target_compile_features(flappedear_event_project_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_event_project_tests PRIVATE flappedear_core Qt6::Test)
target_compile_definitions(flappedear_event_project_tests PRIVATE
    EVENT_PROJECT_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/event-demo.fetproject"
)
flappedear_enable_warnings(flappedear_event_project_tests)
add_test(NAME flappedear_event_project_tests COMMAND flappedear_event_project_tests)
qt_add_executable(flappedear_lap_eligibility_tests LapEligibilityTests.cpp)
target_compile_features(flappedear_lap_eligibility_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_lap_eligibility_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_lap_eligibility_tests)
add_test(NAME flappedear_lap_eligibility_tests COMMAND flappedear_lap_eligibility_tests)
qt_add_executable(flappedear_export_log_tests PersistentExportLogTests.cpp)
target_compile_features(flappedear_export_log_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_export_log_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_export_log_tests)
add_test(NAME flappedear_export_log_tests COMMAND flappedear_export_log_tests)
qt_add_executable(flappedear_track_progress_tests TrackProgressTests.cpp EventProjectFixture.h)
target_compile_features(flappedear_track_progress_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_track_progress_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_track_progress_tests)
add_test(NAME flappedear_track_progress_tests COMMAND flappedear_track_progress_tests)
qt_add_executable(flappedear_track_segments_tests TrackSegmentsTests.cpp)
target_compile_features(flappedear_track_segments_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_track_segments_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_track_segments_tests)
add_test(NAME flappedear_track_segments_tests COMMAND flappedear_track_segments_tests)
qt_add_executable(flappedear_track_segment_proposal_tests TrackSegmentProposalTests.cpp)
target_compile_features(flappedear_track_segment_proposal_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_track_segment_proposal_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_track_segment_proposal_tests)
add_test(NAME flappedear_track_segment_proposal_tests COMMAND flappedear_track_segment_proposal_tests)
qt_add_executable(flappedear_track_segment_review_tests TrackSegmentReviewTests.cpp)
target_compile_features(flappedear_track_segment_review_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_track_segment_review_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_track_segment_review_tests)
add_test(NAME flappedear_track_segment_review_tests COMMAND flappedear_track_segment_review_tests)
qt_add_executable(flappedear_track_segment_editing_tests TrackSegmentEditingTests.cpp)
target_compile_features(flappedear_track_segment_editing_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_track_segment_editing_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_track_segment_editing_tests)
add_test(NAME flappedear_track_segment_editing_tests COMMAND flappedear_track_segment_editing_tests)
qt_add_executable(flappedear_sector_timing_tests SectorTimingTests.cpp)
target_compile_features(flappedear_sector_timing_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_sector_timing_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_sector_timing_tests)
add_test(NAME flappedear_sector_timing_tests COMMAND flappedear_sector_timing_tests)
qt_add_executable(flappedear_theoretical_best_tests TheoreticalBestTests.cpp)
target_compile_features(flappedear_theoretical_best_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_theoretical_best_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_theoretical_best_tests)
add_test(NAME flappedear_theoretical_best_tests COMMAND flappedear_theoretical_best_tests)
qt_add_executable(flappedear_time_loss_tests TimeLossTests.cpp)
target_compile_features(flappedear_time_loss_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_time_loss_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_time_loss_tests)
add_test(NAME flappedear_time_loss_tests COMMAND flappedear_time_loss_tests)
qt_add_executable(flappedear_consistency_tests ConsistencyTests.cpp)
target_compile_features(flappedear_consistency_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_consistency_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_consistency_tests)
add_test(NAME flappedear_consistency_tests COMMAND flappedear_consistency_tests)
qt_add_executable(flappedear_gg_pairs_tests GgPairsTests.cpp)
target_compile_features(flappedear_gg_pairs_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_gg_pairs_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_gg_pairs_tests)
add_test(NAME flappedear_gg_pairs_tests COMMAND flappedear_gg_pairs_tests)
qt_add_executable(flappedear_driving_variability_tests DrivingVariabilityTests.cpp)
target_compile_features(flappedear_driving_variability_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_driving_variability_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_driving_variability_tests)
add_test(NAME flappedear_driving_variability_tests COMMAND flappedear_driving_variability_tests)
qt_add_executable(flappedear_braking_metrics_tests BrakingMetricsTests.cpp)
target_compile_features(flappedear_braking_metrics_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_braking_metrics_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_braking_metrics_tests)
add_test(NAME flappedear_braking_metrics_tests COMMAND flappedear_braking_metrics_tests)
qt_add_executable(flappedear_exit_metrics_tests ExitMetricsTests.cpp)
target_compile_features(flappedear_exit_metrics_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_exit_metrics_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_exit_metrics_tests)
add_test(NAME flappedear_exit_metrics_tests COMMAND flappedear_exit_metrics_tests)
qt_add_executable(flappedear_corner_phase_tests CornerPhaseTests.cpp)
target_compile_features(flappedear_corner_phase_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_corner_phase_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_corner_phase_tests)
add_test(NAME flappedear_corner_phase_tests COMMAND flappedear_corner_phase_tests)
qt_add_executable(flappedear_braking_onset_tests BrakingOnsetTests.cpp)
target_compile_features(flappedear_braking_onset_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_braking_onset_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_braking_onset_tests)
add_test(NAME flappedear_braking_onset_tests COMMAND flappedear_braking_onset_tests)
add_test(NAME flappedear_startup_smoke COMMAND flappedear_native --startup-smoke)
set_tests_properties(flappedear_startup_smoke PROPERTIES
    # Startup checks QML loading and controller/window lifecycle. Offscreen QPA
    # cannot supply a native window swapchain; QRhi pixels are tested separately.
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    PASS_REGULAR_EXPRESSION "Startup smoke passed"
    FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;Binding loop"
)
