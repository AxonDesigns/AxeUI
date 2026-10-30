#pragma once

#if defined(_WIN32)
#include <crtdbg.h>
#include <stdlib.h>
#endif

namespace ui::test {

// Makes a crashing test process report and exit instead of hanging on a
// dialog box (Windows debug CRT). No effect elsewhere.
inline void configure_process_for_tests() noexcept {
#if defined(_WIN32)
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
}

}  // namespace ui::test
