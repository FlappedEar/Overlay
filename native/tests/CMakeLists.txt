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
    UserGuideCapture.cpp
    UserGuideCapture.h
    ../src/app/AppControllerDocument.cpp
    TelemetryTests.cpp
    ../src/app/AppController.cpp
    ../src/app/AppController.h
    ../src/app/BundledFonts.cpp
    ../src/app/BundledFonts.h
)
target_compile_features(flappedear_native_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_native_tests PRIVATE flappedear_core Qt6::Concurrent Qt6::Gui Qt6::GuiPrivate Qt6::Multimedia Qt6::QuickControls2 Qt6::Test ZLIB::ZLIBSTATIC)
flappedear_enable_warnings(flappedear_native_tests)
qt_add_resources(flappedear_native_tests "flappedear_test_qml"
    PREFIX "/qt/qml/FlappedEar/qml"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../qml"
    FILES
        ../qml/TelemetryScene.qml
        ../qml/widgets/LapTimeWidget.qml
        ../qml/widgets/DesignedElements.qml
        ../qml/widgets/DesignedWidget.qml
        ../qml/widgets/RetroTachometerWidget.qml
        ../qml/widgets/PedalsWidget.qml
        ../qml/widgets/TelemetryPanel.qml
        ../qml/widgets/TyresWidget.qml
)
# The application's logo and fonts, so production QML loaded by tests (and the user-guide
# capture) shows it as the application does.
qt_add_resources(flappedear_native_tests "flappedear_test_branding"
    PREFIX "/flappedear"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/.."
    FILES
        ../resources/branding/app-logo.png
        ../resources/fonts/Sora-400.ttf
        ../resources/fonts/Sora-500.ttf
        ../resources/fonts/Sora-600.ttf
        ../resources/fonts/Sora-700.ttf
        ../resources/fonts/JetBrainsMono-500.ttf
        ../resources/fonts/JetBrainsMono-600.ttf
        ../resources/fonts/JetBrainsMono-700.ttf
)
target_compile_definitions(flappedear_native_tests PRIVATE
    GUI_SESSION_LOCK_HELPER_PATH="$<TARGET_FILE:flappedear_gui_session_lock_helper>"
    TEST_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/basic.vbo"
    QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../qml"
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
target_link_libraries(flappedear_rcz_tests PRIVATE flappedear_telemetry_core Qt6::Test ZLIB::ZLIBSTATIC)
flappedear_enable_warnings(flappedear_rcz_tests)
add_test(NAME flappedear_rcz_tests COMMAND flappedear_rcz_tests)
qt_add_executable(flappedear_import_tests TelemetryImportTests.cpp RczFixture.h)
target_compile_features(flappedear_import_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_import_tests PRIVATE flappedear_telemetry_core Qt6::Test ZLIB::ZLIBSTATIC)
flappedear_enable_warnings(flappedear_import_tests)
add_test(NAME flappedear_import_tests COMMAND flappedear_import_tests)
qt_add_executable(flappedear_source_cache_tests TelemetrySessionCacheTests.cpp RczFixture.h)
target_compile_features(flappedear_source_cache_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_source_cache_tests PRIVATE flappedear_telemetry_core Qt6::Test ZLIB::ZLIBSTATIC)
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
target_link_libraries(flappedear_lap_eligibility_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_lap_eligibility_tests)
add_test(NAME flappedear_lap_eligibility_tests COMMAND flappedear_lap_eligibility_tests)
qt_add_executable(flappedear_export_log_tests PersistentExportLogTests.cpp)
target_compile_features(flappedear_export_log_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_export_log_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_export_log_tests)
add_test(NAME flappedear_export_log_tests COMMAND flappedear_export_log_tests)
# KAN-125: Qt Core only, like the session-lock helper: the migration must not need Gui.
qt_add_executable(flappedear_storage_migration_tests
    StorageMigrationTests.cpp
    ../src/app/LegacyStorageMigration.cpp
)
target_compile_features(flappedear_storage_migration_tests PRIVATE cxx_std_20)
target_include_directories(flappedear_storage_migration_tests PRIVATE ../src)
target_link_libraries(flappedear_storage_migration_tests PRIVATE Qt6::Core Qt6::Test)
flappedear_enable_warnings(flappedear_storage_migration_tests)
add_test(NAME flappedear_storage_migration_tests COMMAND flappedear_storage_migration_tests)
# KAN-178: Qt Core only; the parser runs before any Gui start-up.
qt_add_executable(flappedear_command_line_tests
    CommandLineTests.cpp
    ../src/app/CommandLine.cpp
)
target_compile_features(flappedear_command_line_tests PRIVATE cxx_std_20)
target_include_directories(flappedear_command_line_tests PRIVATE ../src)
target_link_libraries(flappedear_command_line_tests PRIVATE Qt6::Core Qt6::Test)
flappedear_enable_warnings(flappedear_command_line_tests)
add_test(NAME flappedear_command_line_tests COMMAND flappedear_command_line_tests)
add_test(NAME flappedear_command_line_usage
    COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:flappedear_native>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckCommandLineUsage.cmake)
# KAN-180: content ids shared with FlappedEar Telemetry (KAN-170).
qt_add_executable(flappedear_content_id_vector_tests ContentIdVectorTests.cpp)
target_compile_features(flappedear_content_id_vector_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_content_id_vector_tests PRIVATE flappedear_telemetry_core Qt6::Test)
target_compile_definitions(flappedear_content_id_vector_tests PRIVATE
    CONTENT_ID_VECTORS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/content-id-vectors.json"
)
flappedear_enable_warnings(flappedear_content_id_vector_tests)
add_test(NAME flappedear_content_id_vector_tests COMMAND flappedear_content_id_vector_tests)
qt_add_executable(flappedear_track_progress_tests TrackProgressTests.cpp EventProjectFixture.h)
target_compile_features(flappedear_track_progress_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_track_progress_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_track_progress_tests)
add_test(NAME flappedear_track_progress_tests COMMAND flappedear_track_progress_tests)
qt_add_executable(flappedear_track_segments_tests TrackSegmentsTests.cpp)
target_compile_features(flappedear_track_segments_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_track_segments_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_track_segments_tests)
add_test(NAME flappedear_track_segments_tests COMMAND flappedear_track_segments_tests)
qt_add_executable(flappedear_track_segment_proposal_tests TrackSegmentProposalTests.cpp)
target_compile_features(flappedear_track_segment_proposal_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_track_segment_proposal_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_track_segment_proposal_tests)
add_test(NAME flappedear_track_segment_proposal_tests COMMAND flappedear_track_segment_proposal_tests)
qt_add_executable(flappedear_track_segment_review_tests TrackSegmentReviewTests.cpp)
target_compile_features(flappedear_track_segment_review_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_track_segment_review_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_track_segment_review_tests)
add_test(NAME flappedear_track_segment_review_tests COMMAND flappedear_track_segment_review_tests)
qt_add_executable(flappedear_track_segment_editing_tests TrackSegmentEditingTests.cpp)
target_compile_features(flappedear_track_segment_editing_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_track_segment_editing_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_track_segment_editing_tests)
add_test(NAME flappedear_track_segment_editing_tests COMMAND flappedear_track_segment_editing_tests)
qt_add_executable(flappedear_sector_timing_tests SectorTimingTests.cpp)
target_compile_features(flappedear_sector_timing_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_sector_timing_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_sector_timing_tests)
add_test(NAME flappedear_sector_timing_tests COMMAND flappedear_sector_timing_tests)
qt_add_executable(flappedear_theoretical_best_tests TheoreticalBestTests.cpp)
target_compile_features(flappedear_theoretical_best_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_theoretical_best_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_theoretical_best_tests)
add_test(NAME flappedear_theoretical_best_tests COMMAND flappedear_theoretical_best_tests)
qt_add_executable(flappedear_time_loss_tests TimeLossTests.cpp)
target_compile_features(flappedear_time_loss_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_time_loss_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_time_loss_tests)
add_test(NAME flappedear_time_loss_tests COMMAND flappedear_time_loss_tests)
qt_add_executable(flappedear_consistency_tests ConsistencyTests.cpp)
target_compile_features(flappedear_consistency_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_consistency_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_consistency_tests)
add_test(NAME flappedear_consistency_tests COMMAND flappedear_consistency_tests)
qt_add_executable(flappedear_gg_pairs_tests GgPairsTests.cpp)
target_compile_features(flappedear_gg_pairs_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_gg_pairs_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_gg_pairs_tests)
add_test(NAME flappedear_gg_pairs_tests COMMAND flappedear_gg_pairs_tests)
qt_add_executable(flappedear_channel_summary_tests ChannelSummaryTests.cpp)
target_compile_features(flappedear_channel_summary_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_channel_summary_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_channel_summary_tests)
add_test(NAME flappedear_channel_summary_tests COMMAND flappedear_channel_summary_tests)
qt_add_executable(flappedear_day_report_tests DayReportTests.cpp)
target_compile_features(flappedear_day_report_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_day_report_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_day_report_tests)
add_test(NAME flappedear_day_report_tests COMMAND flappedear_day_report_tests)
qt_add_executable(flappedear_focus_areas_tests FocusAreasTests.cpp)
target_compile_features(flappedear_focus_areas_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_focus_areas_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_focus_areas_tests)
add_test(NAME flappedear_focus_areas_tests COMMAND flappedear_focus_areas_tests)
qt_add_executable(flappedear_outing_pipeline_tests OutingPipelineTests.cpp)
target_compile_features(flappedear_outing_pipeline_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_outing_pipeline_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_outing_pipeline_tests)
add_test(NAME flappedear_outing_pipeline_tests COMMAND flappedear_outing_pipeline_tests)
qt_add_executable(flappedear_telemetry_core_tests TelemetryCoreTests.cpp)
target_compile_features(flappedear_telemetry_core_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_telemetry_core_tests PRIVATE flappedear_telemetry_core Qt6::Test)
target_compile_definitions(flappedear_telemetry_core_tests PRIVATE
    TEST_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/basic.vbo"
)
flappedear_enable_warnings(flappedear_telemetry_core_tests)
add_test(NAME flappedear_telemetry_core_tests COMMAND flappedear_telemetry_core_tests)
qt_add_executable(flappedear_telemetry_app_tests TelemetryAppTests.cpp)
target_compile_features(flappedear_telemetry_app_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_telemetry_app_tests PRIVATE flappedear_telemetry_app Qt6::Test)
flappedear_enable_warnings(flappedear_telemetry_app_tests)
add_test(NAME flappedear_telemetry_app_tests COMMAND flappedear_telemetry_app_tests)
qt_add_executable(flappedear_driving_variability_tests DrivingVariabilityTests.cpp)
target_compile_features(flappedear_driving_variability_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_driving_variability_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_driving_variability_tests)
add_test(NAME flappedear_driving_variability_tests COMMAND flappedear_driving_variability_tests)
qt_add_executable(flappedear_braking_metrics_tests BrakingMetricsTests.cpp)
target_compile_features(flappedear_braking_metrics_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_braking_metrics_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_braking_metrics_tests)
add_test(NAME flappedear_braking_metrics_tests COMMAND flappedear_braking_metrics_tests)
qt_add_executable(flappedear_exit_metrics_tests ExitMetricsTests.cpp)
target_compile_features(flappedear_exit_metrics_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_exit_metrics_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_exit_metrics_tests)
add_test(NAME flappedear_exit_metrics_tests COMMAND flappedear_exit_metrics_tests)
qt_add_executable(flappedear_corner_phase_tests CornerPhaseTests.cpp)
target_compile_features(flappedear_corner_phase_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_corner_phase_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_corner_phase_tests)
add_test(NAME flappedear_corner_phase_tests COMMAND flappedear_corner_phase_tests)
qt_add_executable(flappedear_driving_states_tests DrivingStatesTests.cpp)
target_compile_features(flappedear_driving_states_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_driving_states_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_driving_states_tests)
add_test(NAME flappedear_driving_states_tests COMMAND flappedear_driving_states_tests)
qt_add_executable(flappedear_map_layers_tests MapLayersTests.cpp)
target_compile_features(flappedear_map_layers_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_map_layers_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_map_layers_tests)
add_test(NAME flappedear_map_layers_tests COMMAND flappedear_map_layers_tests)
qt_add_executable(flappedear_recording_alignment_tests RecordingAlignmentTests.cpp)
target_compile_features(flappedear_recording_alignment_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_recording_alignment_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_recording_alignment_tests)
add_test(NAME flappedear_recording_alignment_tests COMMAND flappedear_recording_alignment_tests)
qt_add_executable(flappedear_channel_fusion_tests ChannelFusionTests.cpp)
target_compile_features(flappedear_channel_fusion_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_channel_fusion_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_channel_fusion_tests)
add_test(NAME flappedear_channel_fusion_tests COMMAND flappedear_channel_fusion_tests)
qt_add_executable(flappedear_gopro_chapters_tests GoProChaptersTests.cpp)
target_compile_features(flappedear_gopro_chapters_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_gopro_chapters_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_gopro_chapters_tests)
add_test(NAME flappedear_gopro_chapters_tests COMMAND flappedear_gopro_chapters_tests)
qt_add_executable(flappedear_media_timeline_tests MediaTimelineTests.cpp)
target_compile_features(flappedear_media_timeline_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_media_timeline_tests PRIVATE flappedear_core Qt6::Test)
flappedear_enable_warnings(flappedear_media_timeline_tests)
add_test(NAME flappedear_media_timeline_tests COMMAND flappedear_media_timeline_tests)
qt_add_executable(flappedear_temperature_association_tests TemperatureAssociationTests.cpp)
target_compile_features(flappedear_temperature_association_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_temperature_association_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_temperature_association_tests)
add_test(NAME flappedear_temperature_association_tests COMMAND flappedear_temperature_association_tests)
qt_add_executable(flappedear_braking_onset_tests BrakingOnsetTests.cpp)
target_compile_features(flappedear_braking_onset_tests PRIVATE cxx_std_20)
target_link_libraries(flappedear_braking_onset_tests PRIVATE flappedear_telemetry_core Qt6::Test)
flappedear_enable_warnings(flappedear_braking_onset_tests)
add_test(NAME flappedear_braking_onset_tests COMMAND flappedear_braking_onset_tests)
add_test(NAME flappedear_startup_smoke COMMAND flappedear_native --startup-smoke)
set_tests_properties(flappedear_startup_smoke PROPERTIES
    # Startup checks QML loading and controller/window lifecycle. Offscreen QPA
    # cannot supply a native window swapchain; QRhi pixels are tested separately.
    # KAN-176: Windows GUI executables otherwise send Qt messages to OutputDebugString,
    # so a local CTest run could neither see the pass line nor catch a QML error.
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    PASS_REGULAR_EXPRESSION "Startup smoke passed"
    FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;Binding loop"
)
add_test(NAME flappedear_telemetry_core_boundary
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../src -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckTelemetryCoreBoundary.cmake)
add_test(NAME flappedear_telemetry_app_boundary
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../src -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckTelemetryAppBoundary.cmake)
