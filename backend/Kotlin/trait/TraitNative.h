#pragma once

#include <memory>
#include <vector>
#include "../../src/types.h"

namespace script::kotlin_backend {
class KotlinEngine;
struct KotlinValue;

struct ArgumentsData {
  KotlinEngine* engine = nullptr;
  std::shared_ptr<KotlinValue> thiz;
  std::vector<std::shared_ptr<KotlinValue>> args;
};

struct ScriptClassState {
  ScriptEngine* scriptEngine = nullptr;
  Weak<Object> weakRef;
};
}  // namespace script::kotlin_backend

namespace script {
template <>
struct internal::ImplType<Arguments> {
  using type = kotlin_backend::ArgumentsData;
};

template <>
struct internal::ImplType<ScriptClass> {
  using type = kotlin_backend::ScriptClassState;
};
}  // namespace script
