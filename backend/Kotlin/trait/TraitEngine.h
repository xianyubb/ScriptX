#pragma once

#include <string>
#include "../../src/types.h"

#define KOTLIN_NOT_IMPLEMENTED() \
  throw Exception(std::string(__func__) + " is not supported by the embedded Kotlin backend")

namespace script {
namespace kotlin_backend {
class KotlinEngine;
}

template <>
struct internal::ImplType<ScriptEngine> {
  using type = kotlin_backend::KotlinEngine;
};
}  // namespace script
