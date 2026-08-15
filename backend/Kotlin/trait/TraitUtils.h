#pragma once

#include <string>
#include "../../src/types.h"

namespace script {
namespace kotlin_backend {
struct KotlinInterop;
}

template <>
struct internal::ImplType<StringHolder> {
  using type = std::string;
};

template <>
struct internal::ImplType<internal::interop> {
  using type = kotlin_backend::KotlinInterop;
};
}  // namespace script
