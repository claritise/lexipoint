#if LEXIRISE

#include "KeyCheck.h"

namespace lexipoint::api {

const char* keyStateName(const KeyState state) {
  switch (state) {
    case KeyState::Checking:
      return "checking";
    case KeyState::NoKey:
      return "no-key";
    case KeyState::Connected:
      return "connected";
    case KeyState::Rejected:
      return "rejected";
    case KeyState::Offline:
      return "offline";
    case KeyState::Error:
      return "error";
    case KeyState::Unchecked:
    default:
      return "unchecked";
  }
}

KeyStatus keyStatusFrom(const ApiResponse& response) {
  KeyStatus status;
  status.error = response.error;
  switch (response.error) {
    case ApiError::None:
      if (parseMe(response.body, status.me) == ParseStatus::Ok) {
        status.state = KeyState::Connected;
      } else {
        status.state = KeyState::Error;
        status.error = ApiError::Malformed;
      }
      break;
    case ApiError::NotConfigured:
      status.state = KeyState::NoKey;
      break;
    case ApiError::Unauthorized:
      status.state = KeyState::Rejected;
      break;
    case ApiError::NoWifi:
    case ApiError::Network:
    case ApiError::Timeout:
    case ApiError::ClockNotSet:
      status.state = KeyState::Offline;
      break;
    case ApiError::LowMemory:
    case ApiError::Tls:
    case ApiError::RateLimited:
    case ApiError::Server:
    case ApiError::Http:
    case ApiError::Malformed:
    default:
      status.state = KeyState::Error;
      break;
  }
  return status;
}

}  // namespace lexipoint::api

#endif  // LEXIRISE
