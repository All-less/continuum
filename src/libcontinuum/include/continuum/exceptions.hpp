#ifndef CONTINUUM_EXCEPTIONS_HPP
#define CONTINUUM_EXCEPTIONS_HPP

#include <stdexcept>

namespace continuum {

class PredictError : public std::runtime_error {
 public:
  PredictError(const std::string msg);

  const char *what() const noexcept;

 private:
  const std::string msg_;
};

class ManagementOperationError : public std::runtime_error {
 public:
  ManagementOperationError(const std::string msg);

  const char *what() const noexcept;

 private:
  const std::string msg_;
};

}  // namespace continuum

#endif  // CONTINUUM_EXCEPTIONS_HPP
