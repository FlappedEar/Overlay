# KAN-178: an unknown option, or a known one with the wrong argument count,
# must print the usage and exit non-zero before the editor or its session lock
# starts. A started editor would never exit, so the timeout catches it.
# Run as: cmake -DAPP=<flappedear_native> -P CheckCommandLineUsage.cmake
foreach(case "--export-test;x;y" "--startup-smoke;extra")
    execute_process(COMMAND "${APP}" ${case}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
    if(result EQUAL 0 OR NOT result MATCHES "^[0-9]+$")
        message(FATAL_ERROR "'${case}' did not fail with a usage error (result: ${result})")
    endif()
    if(NOT error MATCHES "Usage:" OR NOT error MATCHES "--startup-smoke")
        message(FATAL_ERROR "'${case}' printed no usage:\n${output}${error}")
    endif()
endforeach()
message(STATUS "Unknown and malformed options report a usage error")
