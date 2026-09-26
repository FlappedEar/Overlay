# KAN-123: flappedear_telemetry_core (src/telemetry, src/project) must stay
# buildable for iOS/Android: no overlay, video, export, app or Gui code.
# Run as: cmake -DSOURCE_DIR=<native/src> -P CheckTelemetryCoreBoundary.cmake
file(GLOB_RECURSE files "${SOURCE_DIR}/telemetry/*" "${SOURCE_DIR}/project/*")
set(violations "")
foreach(file IN LISTS files)
    file(STRINGS "${file}" lines REGEX "^[ \t]*#[ \t]*include")
    foreach(line IN LISTS lines)
        if(line MATCHES "\"(export|gopro|sync|widgets|app)/"
           OR line MATCHES "<(QtGui|QtQml|QtQuick|QtMultimedia|QGuiApplication|QProcess|QQml|QQuick|QMedia|QImage|QPainter|QColor|rhi/)")
            string(APPEND violations "\n  ${file}: ${line}")
        endif()
    endforeach()
endforeach()
if(violations)
    message(FATAL_ERROR "Telemetry core boundary violated:${violations}")
endif()
message(STATUS "Telemetry core boundary holds (${CMAKE_CURRENT_LIST_DIR})")
