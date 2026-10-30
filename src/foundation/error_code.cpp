#include "foundation/error_code.h"

namespace ui::foundation {

const char* error_code_name(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Ok:
      return "Ok";
    case ErrorCode::InvalidArgument:
      return "InvalidArgument";
    case ErrorCode::OutOfMemory:
      return "OutOfMemory";
    case ErrorCode::InvalidHandle:
      return "InvalidHandle";
    case ErrorCode::StaleHandle:
      return "StaleHandle";
    case ErrorCode::WrongHandleType:
      return "WrongHandleType";
    case ErrorCode::CapacityExceeded:
      return "CapacityExceeded";
    case ErrorCode::Unsupported:
      return "Unsupported";
    case ErrorCode::Unavailable:
      return "Unavailable";
    case ErrorCode::InvalidState:
      return "InvalidState";
    case ErrorCode::Internal:
      return "Internal";
  }
  return "Unknown";
}

}  // namespace ui::foundation
