find_package(Qt6 6.8 REQUIRED COMPONENTS Concurrent Core Gui Qml Quick QuickControls2 Multimedia Test)

# Qt 6.8 exports GuiPrivate with Gui; newer SDKs provide a separate package.
# In both layouts, require the private target from this exact Qt installation.
if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6 ${Qt6_VERSION} EXACT REQUIRED COMPONENTS GuiPrivate)
endif()

qt_standard_project_setup(REQUIRES 6.8)

function(flappedear_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Bounded streaming ZIP inflation for native RCZ input. Pin source and integrity.
include(FetchContent)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(flappedear_zlib
    URL https://zlib.net/fossils/zlib-1.3.2.tar.gz
        https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz
    URL_HASH SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(flappedear_zlib)

qt_add_library(flappedear_core STATIC
    src/app/ApplicationIdentity.h
    src/app/GuiSessionLock.cpp
    src/app/GuiSessionLock.h
    src/app/AppLog.cpp
    src/app/AppLog.h
    src/app/PreviewPlayback.cpp
    src/app/PreviewPlayback.h
    src/export/EncoderDetector.cpp
    src/export/EncoderDetector.h
    src/export/BoundedProcessOutput.cpp
    src/export/BoundedProcessOutput.h
    src/export/ExportDiagnostics.cpp
    src/export/ExportDiagnostics.h
    src/export/ExportArtifactManifest.cpp
    src/export/ExportArtifactManifest.h
    src/export/ExportCancellation.cpp
    src/export/ExportCancellation.h
    src/export/ExportEngine.cpp
    src/export/FinalOutputValidation.cpp
    src/export/FinalOutputValidation.h
    src/export/ExportMediaProfile.cpp
    src/export/ExportMediaProfile.h
    src/export/ExportFormat.cpp
    src/export/ExportEngine.h
    src/export/ExportOutputTransaction.cpp
    src/export/ExportOutputTransaction.h
    src/export/ExportProgress.cpp
    src/export/ExportProgress.h
    src/export/ExportProcessSupervisor.cpp
    src/export/ExportProcessSupervisor.h
    src/export/ExportStoragePolicy.cpp
    src/export/ExportStoragePolicy.h
    src/export/ExportTargetIdentity.cpp
    src/export/ExportTargetIdentity.h
    src/export/FfmpegTools.cpp
    src/export/FfmpegTools.h
    src/export/MediaProbe.cpp
    src/export/MediaProbe.h
    src/app/PreviewPlayback.h
    src/export/PersistentExportLog.cpp
    src/export/PersistentExportLog.h
    src/export/RawFrameTransport.cpp
    src/export/RawFrameTransport.h
    src/export/TelemetryFrameRenderer.cpp
    src/export/TelemetryFrameRenderer.h
    src/export/TemporaryOverlayValidation.cpp
    src/export/TemporaryOverlayValidation.h
    src/gopro/GoProTelemetrySource.cpp
    src/gopro/GoProTelemetrySource.h
    src/project/ProjectWriter.cpp
    src/project/ProjectWriter.h
    src/project/BoundedJsonLoader.cpp
    src/project/BoundedJsonLoader.h
    src/project/ProjectLimits.cpp
    src/project/ProjectLimits.h
    src/project/ProjectDocumentState.cpp
    src/project/ProjectDocumentState.h
    src/project/ProjectRecoveryStore.cpp
    src/project/ProjectRecoveryStore.h
    src/project/ProjectSourceReference.cpp
    src/project/ProjectSourceReference.h
    src/project/EventProjectCodec.cpp
    src/project/EventProjectCodec.h
    src/sync/TelemetrySyncEngine.cpp
    src/sync/TelemetrySyncEngine.h
    src/telemetry/TelemetrySession.cpp
    src/telemetry/TelemetrySession.h
    src/telemetry/TelemetryGeometry.cpp
    src/telemetry/TelemetryGeometry.h
    src/telemetry/LapTiming.cpp
    src/telemetry/LapTiming.h
    src/telemetry/OutingLaps.cpp
    src/telemetry/OutingLaps.h
    src/telemetry/TimingGate.h
    src/telemetry/SourceOperation.h
    src/telemetry/TelemetryRenderContext.cpp
    src/telemetry/TelemetryRenderContext.h
    src/telemetry/TrackGeometry.cpp
    src/telemetry/TrackInference.cpp
    src/telemetry/TrackInference.h
    src/telemetry/TrackGeometry.h
    src/telemetry/TrackProgress.cpp
    src/telemetry/TrackProgress.h
    src/telemetry/TrackSegments.cpp
    src/telemetry/TrackSegments.h
    src/telemetry/TrackSegmentProposals.cpp
    src/telemetry/TrackSegmentProposals.h
    src/telemetry/TrackSegmentReview.cpp
    src/telemetry/TrackSegmentReview.h
    src/telemetry/TrackSegmentEditing.cpp
    src/telemetry/TrackSegmentEditing.h
    src/telemetry/SectorTiming.cpp
    src/telemetry/SectorTiming.h
    src/telemetry/TheoreticalBest.cpp
    src/telemetry/TheoreticalBest.h
    src/telemetry/TimeLoss.cpp
    src/telemetry/TimeLoss.h
    src/telemetry/Consistency.cpp
    src/telemetry/Consistency.h
    src/telemetry/GgPairs.cpp
    src/telemetry/GgPairs.h
    src/telemetry/DrivingVariability.cpp
    src/telemetry/DrivingVariability.h
    src/telemetry/CornerSpeeds.cpp
    src/telemetry/CornerSpeeds.h
    src/telemetry/BrakingMetrics.cpp
    src/telemetry/BrakingMetrics.h
    src/telemetry/ExitMetrics.cpp
    src/telemetry/ExitMetrics.h
    src/telemetry/CornerPhases.cpp
    src/telemetry/CornerPhases.h
    src/telemetry/MetricProvenance.h
    src/telemetry/BrakingOnset.cpp
    src/telemetry/BrakingOnset.h
    src/telemetry/RczParser.cpp
    src/telemetry/RczParser.h
    src/telemetry/TelemetrySessionCache.cpp
    src/telemetry/TelemetrySource.cpp
    src/telemetry/TelemetrySource.h
    src/telemetry/TelemetryImportPlan.cpp
    src/telemetry/TelemetryImportPlan.h
    src/telemetry/VboParser.cpp
    src/telemetry/VboParser.h
    src/widgets/WidgetModel.cpp
    src/widgets/WidgetModel.h
)
target_compile_features(flappedear_core PUBLIC cxx_std_20)
target_include_directories(flappedear_core PUBLIC src)
target_link_libraries(flappedear_core PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
# QRhi is a Qt Gui private API. Keep it implementation-only: no public headers
# in flappedear_core expose QRhi types.
target_link_libraries(flappedear_core PRIVATE Qt6::GuiPrivate ZLIB::ZLIBSTATIC)
flappedear_enable_warnings(flappedear_core)

qt_add_resources(flappedear_core "flappedear_templates"
    PREFIX "/flappedear"
    FILES
        resources/widget-templates.json
)

set(FLAPPEDEAR_PLATFORM_ICON)
if(APPLE)
    set(FLAPPEDEAR_APP_ICON resources/branding/FlappedEar.icns)
    set_source_files_properties(${FLAPPEDEAR_APP_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    list(APPEND FLAPPEDEAR_PLATFORM_ICON ${FLAPPEDEAR_APP_ICON})
elseif(WIN32)
    list(APPEND FLAPPEDEAR_PLATFORM_ICON resources/branding/app-icon.rc)
endif()

qt_add_executable(flappedear_native MACOSX_BUNDLE
    src/app/AppControllerImport.cpp
    src/app/AppControllerComparison.cpp
    src/app/AppControllerOuting.cpp
    src/app/AppControllerSegmentReview.cpp
    src/app/AppControllerCornerAnalyzer.cpp
    src/app/AppControllerTheoreticalBest.cpp
    src/app/AppController.cpp
    src/app/AppController.h
    src/main.cpp
    ${FLAPPEDEAR_PLATFORM_ICON}
)
target_compile_features(flappedear_native PRIVATE cxx_std_20)
target_link_libraries(flappedear_native PRIVATE
    flappedear_core
    Qt6::Core
    Qt6::Concurrent
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
)
flappedear_enable_warnings(flappedear_native)

qt_add_resources(flappedear_native "flappedear_branding"
    PREFIX "/flappedear"
    FILES
        resources/branding/app-logo.png
)

qt_add_qml_module(flappedear_native
    URI FlappedEar
    VERSION 1.0
    QML_FILES
        qml/ColorField.qml
        qml/AnalysisPanel.qml
        qml/BatchImportDialog.qml
        qml/AnalysisWindow.qml
        qml/AnalysisStartPanel.qml
        qml/OutingProgressionDialog.qml
        qml/TheoreticalBestDialog.qml
        qml/TimeLossDialog.qml
        qml/SectionProgressionView.qml
        qml/RunDetailsDialog.qml
        qml/OutingLapPanel.qml
        qml/OutingLapDetailPanel.qml
        qml/ComparisonDetailPanel.qml
        qml/ComparisonOverlayChart.qml
        qml/ComparisonOverlayMap.qml
        qml/ComparisonSegmentPanel.qml
        qml/ComparisonGgPanel.qml
        qml/ComparisonLapDialog.qml
        qml/LapTimingPanel.qml
        qml/FeButton.qml
        qml/FeCheckBox.qml
        qml/FeComboBox.qml
        qml/FeSlider.qml
        qml/FeSpinBox.qml
        qml/FeTextField.qml
        qml/InspectorPanel.qml
        qml/StartupError.qml
        qml/Main.qml
        qml/SectionTitle.qml
        qml/SegmentReviewPanel.qml
        qml/TrackMapPanel.qml
        qml/TelemetryScene.qml
        qml/VisualSmokeScene.qml
        qml/WidgetOverlay.qml
        qml/widgets/ArcGaugeWidget.qml
        qml/widgets/BrandLogoWidget.qml
        qml/widgets/CustomValueWidget.qml
        qml/widgets/DialGaugeWidget.qml
        qml/widgets/GForceWidget.qml
        qml/widgets/GForceData.qml
        qml/widgets/F1GForceRadarWidget.qml
        qml/widgets/GForceMagnitudeBarWidget.qml
        qml/widgets/HeartRateWidget.qml
        qml/widgets/ComparisonTileWidget.qml
        qml/widgets/PedalsWidget.qml
        qml/widgets/RetroGearWidget.qml
        qml/widgets/RetroCustomValueWidget.qml
        qml/widgets/RetroGrandPrixWidget.qml
        qml/widgets/RetroNameplateWidget.qml
        qml/widgets/RetroPedalWidget.qml
        qml/widgets/RetroSpeedArcWidget.qml
        qml/widgets/RetroTachometerWidget.qml
        qml/widgets/RpmWidget.qml
        qml/widgets/SpeedWidget.qml
        qml/widgets/TelemetryOverlayWidget.qml
        qml/widgets/TelemetryPanel.qml
        qml/widgets/TrackWidget.qml
    RESOURCES
        qml/assets/change-car-oil-svgrepo-com.svg
        qml/assets/temperature-transmission.svg
        qml/assets/engine-coolant-svgrepo-com.svg
)

set_target_properties(flappedear_native PROPERTIES
    OUTPUT_NAME "Flapped Ear Telemetry"
    MACOSX_BUNDLE_BUNDLE_NAME "Flapped Ear Telemetry"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.flappedear.telemetry"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.2.0"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    MACOSX_BUNDLE_ICON_FILE "FlappedEar.icns"
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS flappedear_native
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
option(FLAPPEDEAR_DEPLOY_QT "Deploy Qt runtime into an internal candidate package" OFF)
if(FLAPPEDEAR_DEPLOY_QT)
    if(NOT APPLE AND NOT WIN32)
        message(FATAL_ERROR "Candidate deployment currently supports macOS and Windows only")
    endif()
    # Qt 6.8's convenience generator emits an unquoted executable argument.
    # Use the supported deployment API with explicit quoting for our display name.
    if(APPLE)
        set(flappedear_deploy_executable "$<TARGET_FILE_NAME:flappedear_native>.app")
    else()
        set(flappedear_deploy_executable "${CMAKE_INSTALL_BINDIR}/$<TARGET_FILE_NAME:flappedear_native>")
    endif()
    qt6_generate_deploy_script(
        TARGET flappedear_native
        OUTPUT_SCRIPT flappedear_deploy_script
        CONTENT "
qt_deploy_qml_imports(TARGET flappedear_native PLUGINS_FOUND flappedear_qml_plugins)
if(NOT flappedear_qml_plugins)
    message(FATAL_ERROR \"QML plugin deployment list is empty\")
endif()
qt_deploy_runtime_dependencies(
    EXECUTABLE \"${flappedear_deploy_executable}\"
    ADDITIONAL_MODULES \${flappedear_qml_plugins}
    GENERATE_QT_CONF
    NO_TRANSLATIONS
)")
    install(SCRIPT ${flappedear_deploy_script})
endif()

add_subdirectory(tests)
