# Runs a program that is expected to abort and checks its diagnostic output.
#
#   cmake -DPROGRAM=<path> [-DEMULATOR=<launcher;args>] -DEXPECTED_REGEX=<regex> -P ui_expect_abort.cmake
#
# Passes when the program exits unsuccessfully (non-zero code or signal) and
# its combined stdout/stderr matches EXPECTED_REGEX. Used for death tests,
# which CTest cannot express directly: PASS_REGULAR_EXPRESSION still fails a
# test whose process was killed by a signal.

if(NOT PROGRAM OR NOT EXPECTED_REGEX)
  message(FATAL_ERROR "PROGRAM and EXPECTED_REGEX are required")
endif()

execute_process(
  COMMAND ${EMULATOR} "${PROGRAM}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output)

message("${output}")
if(result EQUAL 0)
  message(FATAL_ERROR "expected the program to abort, but it exited successfully")
endif()
if(NOT output MATCHES "${EXPECTED_REGEX}")
  message(FATAL_ERROR "output did not match: ${EXPECTED_REGEX}")
endif()
message("program aborted as expected (${result})")
