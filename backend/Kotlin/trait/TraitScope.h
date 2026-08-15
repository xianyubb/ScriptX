#pragma once

#include "TraitEngine.h"

namespace script::kotlin_backend {
class EngineScopeImpl {
 public:
  explicit EngineScopeImpl(KotlinEngine&, KotlinEngine*) {}
};

class ExitEngineScopeImpl {
 public:
  explicit ExitEngineScopeImpl(KotlinEngine&) {}
};

class StackFrameScopeImpl {
 public:
  explicit StackFrameScopeImpl(KotlinEngine&) {}

  template <typename T>
  Local<T> returnValue(const Local<T>& localRef) {
    return localRef;
  }
};
}  // namespace script::kotlin_backend

namespace script {
template <>
struct internal::ImplType<EngineScope> {
  using type = kotlin_backend::EngineScopeImpl;
};
template <>
struct internal::ImplType<ExitEngineScope> {
  using type = kotlin_backend::ExitEngineScopeImpl;
};
template <>
struct internal::ImplType<StackFrameScope> {
  using type = kotlin_backend::StackFrameScopeImpl;
};
}  // namespace script
