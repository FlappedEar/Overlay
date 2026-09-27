# KAN-124: flappedear_telemetry_app (the document and analysis controllers the
# Telemetry app runs) must stay free of overlay, video, export and Qt Gui code,
# so it builds for iOS/Android as well as macOS.
# Run as: cmake -DSOURCE_DIR=<native/src> -P CheckTelemetryAppBoundary.cmake
file(GLOB files
    "${SOURCE_DIR}/app/Analysis*" "${SOURCE_DIR}/app/Document*" "${SOURCE_DIR}/app/Telemetry*"
    "${SOURCE_DIR}/app/VideoLink.h" "${SOURCE_DIR}/app/AppLog.*")
set(violations "")
foreach(file IN LISTS files)
    file(STRINGS "${file}" lines REGEX "^[ \t]*#[ \t]*include")
    foreach(line IN LISTS lines)
        if(line MATCHES "\"(export|gopro|sync|widgets)/"
           OR line MATCHES "\"app/(AppController|PreviewPlayback|GuiSessionLock|ApplicationIdentity)"
           OR line MATCHES "<(QtGui|QtQml|QtQuick|QtMultimedia|QGuiApplication|QProcess|QQml|QQuick|QMedia|QImage|QPainter|QColor|rhi/)")
            string(APPEND violations "\n  ${file}: ${line}")
        endif()
    endforeach()
endforeach()
if(violations)
    message(FATAL_ERROR "Telemetry app boundary violated:${violations}")
endif()
message(STATUS "Telemetry app boundary holds")
