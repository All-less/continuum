#include <sstream>
#include <string>

#include <continuum/exceptions.hpp>

namespace continuum {

PredictError::PredictError(const std::string msg)
    : std::runtime_error(msg), msg_(msg) {}

const char* PredictError::what() const noexcept { return msg_.c_str(); }

ManagementOperationError::ManagementOperationError(const std::string msg)
    : std::runtime_error(msg), msg_(msg) {}

const char* ManagementOperationError::what() const noexcept {
  return msg_.c_str();
}

}  // namespace continuum
