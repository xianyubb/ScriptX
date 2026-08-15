#pragma once

#include <memory>
#include "../../src/types.h"

namespace script::kotlin_backend {
struct KotlinValue;
}

namespace script::internal {
template <typename T>
struct ImplType<Local<T>> {
  using type = std::shared_ptr<kotlin_backend::KotlinValue>;
};

template <typename T>
struct ImplType<Global<T>> {
  using type = std::shared_ptr<kotlin_backend::KotlinValue>;
};

template <typename T>
struct ImplType<Weak<T>> {
  using type = std::weak_ptr<kotlin_backend::KotlinValue>;
};
}  // namespace script::internal
