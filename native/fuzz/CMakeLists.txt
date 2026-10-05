# KAN-155: one libFuzzer executable per untrusted input format. docs/testing.md
# says how to run them; seeds/ holds the starting corpora.
function(flappedear_add_fuzzer name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE flappedear_telemetry_core Qt6::Core)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    flappedear_enable_warnings(${name})
endfunction()

flappedear_add_fuzzer(flappedear_fuzz_vbo FuzzVbo.cpp)
flappedear_add_fuzzer(flappedear_fuzz_rcz FuzzRcz.cpp)
# The GPMF decoder lives in the overlay library; build just its source so the
# harness needs Qt Core only.
flappedear_add_fuzzer(flappedear_fuzz_gpmf FuzzGpmf.cpp
    ../src/gopro/GoProTelemetrySource.cpp ../src/export/FfmpegTools.cpp)
