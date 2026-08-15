#pragma once

#include <string>
#include "../../src/types.h"

namespace script::kotlin_backend {
struct ExceptionFields {
  mutable std::string message;
  mutable std::string stacktrace;
};
}  // namespace script::kotlin_backend

namespace script {
template <>
struct internal::ImplType<Exception> {
  using type = kotlin_backend::ExceptionFields;
};
}  // namespace script
