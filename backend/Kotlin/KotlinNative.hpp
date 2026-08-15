#pragma once

#include "../../src/Native.h"

namespace script {
template <typename T>
T* Arguments::engineAs() const {
  return internal::scriptDynamicCast<T*>(callbackInfo_.engine);
}
}  // namespace script
